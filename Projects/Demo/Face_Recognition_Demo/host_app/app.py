import cv2
import serial
import struct
import time
import mediapipe as mp
import numpy as np
import sys
import threading
import queue

import re

import zlib
import os
from datetime import datetime

# Configuration
SERIAL_PORT = 'COM7'  # Default, can be changed
BAUD_RATE = 921600
FACE_SIZE = (112, 112)
CONSOLE_WIDTH = 400  # Width of the debug console panel


class FaceTransferApp:
    def __init__(self):
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            print("Error: Could not open camera.")
            sys.exit(1)

        # MediaPipe Face Mesh (Lightweight model with 5 keypoints extraction)
        self.mp_face_mesh = mp.solutions.face_mesh
        self.face_mesh = self.mp_face_mesh.FaceMesh(
            max_num_faces=5,
            refine_landmarks=True,
            min_detection_confidence=0.2,  # Lowered to detect faces further away
            min_tracking_confidence=0.5)

        self.serial_port = None
        self.similarity_score = None  # Store similarity score
        self.best_match_id = None     # Store best match target ID
        self.target_count = 0         # Number of registered targets
        self.prev_src_pts = None      # For landmark smoothing

        try:
            self.serial_port = serial.Serial(
                SERIAL_PORT, BAUD_RATE, timeout=0.1)  # Short timeout for non-blocking read loop
            print(f"Opened serial port {SERIAL_PORT}")
        except Exception as e:
            print(f"Error opening serial port {SERIAL_PORT}: {e}")
            print("Running in offline mode (no transfer).")

        self.running = True
        self.auto_mode = False
        self.status_message = "Press 't' to save Target, 'c' to save Compare, 's' for Auto, 'q' to quit."

        # Console buffer
        self.console_lines = []
        self.current_line = ""
        self.console_queue = queue.Queue()  # Use queue for thread safety without locks
        self.buffer_lock = threading.Lock()

        # Create output directory
        self.output_dir = "saved_faces"
        os.makedirs(self.output_dir, exist_ok=True)

        # Serial handling
        self.serial_queue = queue.Queue()
        self.event_queue = queue.Queue()  # Queue for high-level events (results)
        self.serial_buffer = ""  # Buffer for incoming serial data
        self.is_transferring = False
        self.transfer_lock = threading.Lock()  # Prevent concurrent transfers
        self.reader_thread = threading.Thread(
            target=self.read_serial_loop, daemon=True)
        self.reader_thread.start()

    def log_to_console(self, text):
        self.console_queue.put(text)

    def process_incoming_text(self, text):
        print(text, end='', flush=True)
        self.log_to_console(text)

        with self.buffer_lock:
            self.serial_buffer += text
            while '\n' in self.serial_buffer:
                line, self.serial_buffer = self.serial_buffer.split('\n', 1)
                line = line.strip()

                # Parse similarity score
                match = re.search(
                    r'\[SIM\] Best Match: ID=(\d+), Score=(\d+) \(of (\d+) targets\)', line)
                if match:
                    self.best_match_id = int(match.group(1))
                    self.similarity_score = int(match.group(2))
                    self.target_count = int(match.group(3))
                    print(
                        f"\n[PC] Best Match: ID={self.best_match_id}, Score={self.similarity_score}% (of {self.target_count} targets)\n")
                    self.event_queue.put({
                        'type': 'match',
                        'id': self.best_match_id,
                        'score': self.similarity_score
                    })

                # Parse target storage
                match_target = re.search(
                    r'\[M4\] Target feature stored at slot (\d+)', line)
                if match_target:
                    slot_id = int(match_target.group(1))
                    self.event_queue.put({
                        'type': 'target',
                        'id': slot_id
                    })

            if len(self.serial_buffer) > 4096:
                self.serial_buffer = self.serial_buffer[-4096:]

    def read_serial_loop(self):
        while self.running:
            if self.serial_port and self.serial_port.is_open:
                try:
                    if self.serial_port.in_waiting:
                        data = self.serial_port.read(
                            self.serial_port.in_waiting)
                        if self.is_transferring:
                            for b in data:
                                self.serial_queue.put(bytes([b]))
                        else:
                            text = data.decode(errors='ignore')
                            self.process_incoming_text(text)
                    else:
                        time.sleep(0.01)
                except Exception as e:
                    # print(f"Serial read error: {e}")
                    time.sleep(0.1)
            else:
                time.sleep(0.1)

    def wait_for_ack(self, timeout=5):
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                byte = self.serial_queue.get(timeout=0.1)
                if byte == b'K':
                    return True
                elif byte == b'E':
                    return False  # NAK - error from MCU
                else:
                    # Debug info from MCU
                    text = byte.decode(errors='ignore')
                    print(text, end='', flush=True)
                    self.log_to_console(text)
            except queue.Empty:
                continue
        return False

    def send_face_data(self, face_img, command, save_image=True):
        # Ensure is_transferring is True (should be set by caller, but for safety)
        self.is_transferring = True

        try:
            if not self.serial_port or not self.serial_port.is_open:
                self.status_message = "Serial port not open!"
                return

            # Clear queues
            while not self.serial_queue.empty():
                self.serial_queue.get()
            while not self.event_queue.empty():
                self.event_queue.get()

            # Convert to RGB
            face_rgb = cv2.cvtColor(face_img, cv2.COLOR_BGR2RGB)
            data = face_rgb.tobytes()

            # Calculate Checksum
            img_crc = zlib.crc32(data)

            size = len(data)
            chunk_size = 4096
            total_chunks = (size + chunk_size - 1) // chunk_size

            face_name = "Target" if command == 'target' else "Compare"
            addr = "DSP SRAM0 @ 0x44000000"

            msg = f"\n[PC] ========== Starting Transfer ==========\n"
            print(msg, end='')
            self.log_to_console(msg)

            msg = f"[PC] Face: {face_name} | Size: {size} bytes | CRC32: {img_crc:08X}\n"
            print(msg, end='')
            self.log_to_console(msg)

            msg = f"[PC] Destination: Flash @ {addr}\n"
            print(msg, end='')
            self.log_to_console(msg)
            self.status_message = f"Sending {face_name}..."

            # 0. Enter Transfer Mode
            msg = f"[PC] Step 0: Sending 'load' command...\n"
            print(msg, end='')
            self.log_to_console(msg)

            self.serial_port.write(b'load\r')
            time.sleep(0.5)  # Wait for MCU to switch modes
            # Clear queue again to remove "Starting..." messages
            while not self.serial_queue.empty():
                self.serial_queue.get()
            # 1. Send Command
            cmd_byte = b'T' if command == 'target' else b'C'
            msg = f"[PC] Step 1: Sending command '{cmd_byte.decode()}' (0x{cmd_byte[0]:02X})...\n"
            print(msg, end='')
            self.log_to_console(msg)

            self.serial_port.write(cmd_byte)

            msg = f"[PC]   -> Waiting for MCU ACK...\n"
            print(msg, end='')
            self.log_to_console(msg)

            if not self.wait_for_ack(timeout=3):
                raise Exception("No ACK for command")

            msg = f"[PC]   <- Received ACK 'K'\n"
            print(msg, end='')
            self.log_to_console(msg)

            # 2. Send Size
            msg = f"[PC] Step 2: Sending size {size} bytes (0x{size:08X})...\n"
            print(msg, end='')
            self.log_to_console(msg)

            self.serial_port.write(struct.pack('<I', size))

            msg = f"[PC]   -> Waiting for MCU to erase flash...\n"
            print(msg, end='')
            self.log_to_console(msg)

            # Wait for erase ACK (can take time)
            start_erase = time.time()
            if not self.wait_for_ack(timeout=10):
                raise Exception("No ACK after erase")
            erase_time = time.time() - start_erase

            msg = f"[PC]   <- Flash erased (took {erase_time:.2f}s)\n"
            print(msg, end='')
            self.log_to_console(msg)

            # 3. Send Data Chunks
            msg = f"[PC] Step 3: Transferring {total_chunks} chunks...\n"
            print(msg, end='')
            self.log_to_console(msg)

            start_transfer = time.time()

            for i in range(0, size, chunk_size):
                chunk_num = i // chunk_size
                chunk = data[i:i+chunk_size]
                chunk_len = len(chunk)

                msg = f"[PC]   Chunk {chunk_num}/{total_chunks}: {chunk_len} bytes @ offset 0x{i:05X}... "
                print(msg, end='', flush=True)
                self.log_to_console(msg)

                self.serial_port.write(chunk)

                # Wait for chunk ACK
                if not self.wait_for_ack(timeout=3):
                    raise Exception(f"No ACK for chunk {chunk_num}")

                msg = f"ACK\n"
                print(msg, end='')
                self.log_to_console(msg)

                # Progress
                progress = (chunk_num + 1) * 100 // total_chunks
                self.status_message = f"Sending {face_name}... {progress}%"

            transfer_time = time.time() - start_transfer
            speed = size / transfer_time / 1024  # KB/s

            # Drain any remaining data in serial_queue (e.g. result that arrived quickly)
            remaining_bytes = b''
            while not self.serial_queue.empty():
                try:
                    remaining_bytes += self.serial_queue.get_nowait()
                except queue.Empty:
                    break

            if remaining_bytes:
                self.process_incoming_text(
                    remaining_bytes.decode(errors='ignore'))
            # Switch back to text mode so serial reader can parse result messages
            self.is_transferring = False
            msg = f"[PC] ========== Transfer Complete ==========\n"
            print(msg, end='')
            self.log_to_console(msg)

            msg = f"[PC] Time: {transfer_time:.2f}s | Speed: {speed:.1f} KB/s\n"
            print(msg, end='')
            self.log_to_console(msg)

            msg = f"[PC] {face_name} face saved to Flash @ {addr}\n\n"
            print(msg, end='')
            self.log_to_console(msg)

            self.status_message = f"{face_name} face saved!"

            # Wait for result and save image locally
            if save_image:
                msg = f"[PC] Waiting for processing result to save image...\n"
            else:
                msg = f"[PC] Waiting for processing result...\n"

            print(msg, end='')
            self.log_to_console(msg)

            try:
                # Wait up to 5 seconds for the result
                result = self.event_queue.get(timeout=5)

                if save_image:
                    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                    filename = None

                    if result['type'] == 'match':
                        filename = f"id_{result['id']}_score_{result['score']}_{timestamp}.jpg"
                    elif result['type'] == 'target':
                        filename = f"target_id_{result['id']}_{timestamp}.jpg"

                    if filename:
                        filepath = os.path.join(self.output_dir, filename)
                        try:
                            # Use imencode + write to avoid potential cv2.imwrite locking issues
                            success, encoded_img = cv2.imencode(
                                '.jpg', face_img)
                            if success:
                                with open(filepath, "wb") as f:
                                    f.write(encoded_img.tobytes())
                                msg = f"[PC] Saved image to {filepath}\n"
                            else:
                                msg = f"[PC] Error encoding image\n"
                        except Exception as e:
                            msg = f"[PC] Error saving image: {e}\n"

                        print(msg, end='')
                        self.log_to_console(msg)
            except queue.Empty:
                msg = f"[PC] Timeout waiting for result.\n"
                print(msg, end='')
                self.log_to_console(msg)
            except Exception as e:
                msg = f"[PC] Error in result processing: {e}\n"
                print(msg, end='')
                sys.stdout.flush()
                self.log_to_console(msg)

        except Exception as e:
            msg = f"\n[PC] ========== Transfer Failed ==========\n"
            print(msg, end='')
            sys.stdout.flush()
            self.log_to_console(msg)

            msg = f"[PC] Error: {e}\n\n"
            print(msg, end='')
            sys.stdout.flush()
            self.log_to_console(msg)

            self.status_message = f"Failed: {e}"
        finally:
            self.is_transferring = False
            if self.transfer_lock.locked():
                self.transfer_lock.release()

    def calculate_frontal_score(self, landmarks, iw, ih):
        kp = landmarks.landmark
        # 468: Left Eye, 473: Right Eye, 1: Nose
        left_eye = np.array([kp[468].x * iw, kp[468].y * ih])
        right_eye = np.array([kp[473].x * iw, kp[473].y * ih])
        nose = np.array([kp[1].x * iw, kp[1].y * ih])

        face_width = np.linalg.norm(left_eye - right_eye)
        if face_width == 0:
            return 0

        d_left = np.linalg.norm(left_eye - nose)
        d_right = np.linalg.norm(right_eye - nose)

        diff = abs(d_left - d_right)
        ratio = diff / face_width

        # Heuristic: 0.0 diff -> 100 score. 0.5 diff -> 0 score.
        score = max(0, 1.0 - (ratio * 2.0))
        return int(score * 100)

    def get_aligned_face(self, frame, face_landmarks):
        ih, iw, _ = frame.shape
        kp = face_landmarks.landmark

        # Extract 5 keypoints (x, y)
        # MediaPipe Face Mesh Indices (refine_landmarks=True):
        # Left Eye Iris: 468, Right Eye Iris: 473, Nose Tip: 1
        # Left Mouth Corner: 61, Right Mouth Corner: 291

        src_pts = np.array([
            [kp[468].x * iw, kp[468].y * ih],  # Left Eye
            [kp[473].x * iw, kp[473].y * ih],  # Right Eye
            [kp[1].x * iw, kp[1].y * ih],     # Nose
            [kp[61].x * iw, kp[61].y * ih],   # Left Mouth
            [kp[291].x * iw, kp[291].y * ih]  # Right Mouth
        ], dtype=np.float32)

        # Landmark Smoothing (Exponential Moving Average)
        # Reduces jitter when face is stationary
        if self.prev_src_pts is not None:
            # Calculate average movement of landmarks
            diff = np.linalg.norm(src_pts - self.prev_src_pts, axis=1).mean()
            if diff < 20:  # If movement is small (likely same face/jitter)
                # Smoothing factor (0.2 = heavy smoothing, 0.8 = responsive)
                alpha = 0.2
                src_pts = alpha * src_pts + (1 - alpha) * self.prev_src_pts
            else:
                # Large movement, treat as new position (don't smooth)
                pass

        self.prev_src_pts = src_pts

        # Destination points (User provided)
        dst_pts = np.array([
            [30.2946 + 8, 51.6963],  # Left Eye
            [65.5318 + 8, 51.5014],  # Right Eye
            [48.0252 + 8, 71.7366],  # Nose
            [33.5493 + 8, 92.3655],  # Left Mouth Corner
            [62.7299 + 8, 92.2041]   # Right Mouth Corner
        ], dtype=np.float32)

        # Estimate affine transform
        # Use estimateAffine2D (full affine) instead of estimateAffinePartial2D (rigid)
        # This allows for reflection/shear, which might be needed if mapping mirrored face to standard face
        tform, _ = cv2.estimateAffine2D(src_pts, dst_pts)

        if tform is None:
            return None

        aligned_face = cv2.warpAffine(frame, tform, (112, 112))
        return aligned_face

    def run(self):
        while self.running:
            ret, frame = self.cap.read()
            if not ret:
                break

            # Flip for mirror effect
            frame = cv2.flip(frame, 1)
            h, w, _ = frame.shape

            # Detect faces
            frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            results = self.face_mesh.process(frame_rgb)

            face_img_to_send = None
            ih, iw, _ = frame.shape

            # Create a copy for visualization to avoid polluting the original frame with boxes/text
            display_frame = frame.copy()

            if results.multi_face_landmarks:
                best_landmarks = None
                min_dist = float('inf')

                # First pass: Find best face (closest to center)
                for face_landmarks in results.multi_face_landmarks:
                    # Calculate center from key landmarks (e.g. nose tip)
                    nose = face_landmarks.landmark[1]
                    cx, cy = nose.x * iw, nose.y * ih
                    dist = ((cx - iw/2)**2 + (cy - ih/2)**2)**0.5

                    if dist < min_dist:
                        min_dist = dist
                        best_landmarks = face_landmarks

                # Second pass: Draw all faces
                for face_landmarks in results.multi_face_landmarks:
                    is_best = (face_landmarks == best_landmarks)

                    # Calculate bounding box from landmarks
                    x_min, y_min = iw, ih
                    x_max, y_max = 0, 0
                    for lm in face_landmarks.landmark:
                        x, y = int(lm.x * iw), int(lm.y * ih)
                        if x < x_min:
                            x_min = x
                        if x > x_max:
                            x_max = x
                        if y < y_min:
                            y_min = y
                        if y > y_max:
                            y_max = y

                    # Add some padding
                    w_box = x_max - x_min
                    h_box = y_max - y_min
                    pad_x = int(w_box * 0.1)
                    pad_y = int(h_box * 0.1)
                    x = max(0, x_min - pad_x)
                    y = max(0, y_min - pad_y)
                    w_box = min(iw - x, w_box + 2 * pad_x)
                    h_box = min(ih - y, h_box + 2 * pad_y)

                    color = (0, 255, 0) if is_best else (255, 0, 0)
                    thickness = 2 if is_best else 1

                    # Draw bounding box
                    cv2.rectangle(display_frame, (x, y),
                                  (x + w_box, y + h_box), color, thickness)

                    # Calculate and draw quality score (Frontalness)
                    quality_score = self.calculate_frontal_score(
                        face_landmarks, iw, ih)
                    score_text = f"Q:{quality_score}"
                    cv2.putText(display_frame, score_text, (x, y - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

                    # Draw 5 keypoints
                    keypoint_indices = [468, 473, 1, 61, 291]
                    for idx in keypoint_indices:
                        lm = face_landmarks.landmark[idx]
                        kx, ky = int(lm.x * iw), int(lm.y * ih)
                        cv2.circle(display_frame, (kx, ky),
                                   3, (0, 255, 255), -1)

                    if is_best:
                        # Get aligned face
                        face_img_to_send = self.get_aligned_face(
                            frame, face_landmarks)

                        # Fallback if alignment fails
                        if face_img_to_send is None:
                            if w_box > 0 and h_box > 0:
                                face_crop = frame[y:y+h_box, x:x+w_box]
                                try:
                                    face_img_to_send = cv2.resize(
                                        face_crop, FACE_SIZE)
                                except:
                                    pass
            else:
                self.prev_src_pts = None

            # Create combined canvas
            canvas = np.zeros((h, w + CONSOLE_WIDTH, 3), dtype=np.uint8)
            canvas[:h, :w, :] = display_frame

            # Draw status on video
            cv2.putText(canvas, self.status_message, (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

            # Draw Similarity Score if available
            if self.similarity_score is not None:
                # Line 1: Best Match ID and Score
                score_text = f"Best Match: ID={self.best_match_id}, Score={self.similarity_score}%"
                # Line 2: Target count
                count_text = f"Targets: {self.target_count}"

                # Calculate text sizes
                (text_w1, text_h1), _ = cv2.getTextSize(
                    score_text, cv2.FONT_HERSHEY_SIMPLEX, 0.8, 2)
                (text_w2, text_h2), _ = cv2.getTextSize(
                    count_text, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 1)

                box_w = max(text_w1, text_w2) + 20
                box_h = text_h1 + text_h2 + 25

                # Draw background box
                cv2.rectangle(canvas, (10, 50),
                              (10 + box_w, 50 + box_h), (0, 0, 0), -1)

                # Draw score text
                color = (0, 255, 0) if self.similarity_score > 50 else (
                    0, 0, 255)
                cv2.putText(canvas, score_text, (15, 50 + text_h1 + 5),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
                # Draw target count
                cv2.putText(canvas, count_text, (15, 50 + text_h1 + text_h2 + 15),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 200, 200), 1)

            # Draw console
            # Draw separator line
            cv2.line(canvas, (w, 0), (w, h), (100, 100, 100), 1)

            # Process console queue
            try:
                while True:
                    text = self.console_queue.get_nowait()
                    for char in text:
                        if char == '\n':
                            self.console_lines.append(self.current_line)
                            self.current_line = ""
                        elif char == '\r':
                            pass
                        else:
                            self.current_line += char
            except queue.Empty:
                pass

            # Keep buffer size reasonable
            if len(self.console_lines) > 100:
                self.console_lines = self.console_lines[-100:]

            # Draw console text
            lines_to_draw = self.console_lines + [self.current_line]

            font_scale = 0.4
            font_thickness = 1
            line_height = 15
            max_lines = (h - 20) // line_height

            start_line = max(0, len(lines_to_draw) - max_lines)

            y_pos = 20
            for i in range(start_line, len(lines_to_draw)):
                line = lines_to_draw[i]
                # Simple clipping if too long
                if len(line) > 50:
                    line = line[:47] + "..."
                cv2.putText(canvas, line, (w + 10, y_pos),
                            cv2.FONT_HERSHEY_SIMPLEX, font_scale, (0, 255, 0), font_thickness)
                y_pos += line_height

            # Auto Mode Logic
            if self.auto_mode and face_img_to_send is not None:
                if self.transfer_lock.acquire(blocking=False):
                    self.is_transferring = True
                    threading.Thread(target=self.send_face_data, args=(
                        face_img_to_send, 'compare', False)).start()

            cv2.imshow('Face Transfer App', canvas)

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                self.running = False
            elif key == ord('s'):
                self.auto_mode = not self.auto_mode
                if self.auto_mode:
                    self.status_message = "Auto Mode: ON. Press 's' to stop."
                else:
                    self.status_message = "Auto Mode: OFF. Press 't'/'c'/'s'."
            elif key == ord('t'):
                if not self.auto_mode:
                    if face_img_to_send is not None:
                        if self.transfer_lock.acquire(blocking=False):
                            self.is_transferring = True
                            threading.Thread(target=self.send_face_data, args=(
                                face_img_to_send, 'target', True)).start()
                        else:
                            self.status_message = "Transfer in progress..."
                    else:
                        self.status_message = "No face detected!"
                else:
                    self.status_message = "Stop Auto Mode (s) to use 't'."
            elif key == ord('c'):
                if not self.auto_mode:
                    if face_img_to_send is not None:
                        if self.transfer_lock.acquire(blocking=False):
                            self.is_transferring = True
                            threading.Thread(target=self.send_face_data, args=(
                                face_img_to_send, 'compare', True)).start()
                        else:
                            self.status_message = "Transfer in progress..."
                    else:
                        self.status_message = "No face detected!"
                else:
                    self.status_message = "Stop Auto Mode (s) to use 'c'."

        self.cap.release()
        if self.serial_port:
            self.serial_port.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    app = FaceTransferApp()
    app.run()
