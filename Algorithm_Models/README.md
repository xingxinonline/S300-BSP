# S300 Algorithm Models

This directory contains pre-trained **AI Algorithm Models** for the S300 intelligent platform.

## Structure

Each subdirectory includes the resources for a specific S300 AI model, comprising machine code binaries (`model_*.bin`) and metadata (`model_info.json`).

```
Algorithm_Models/
├── Face_Detection/
│   ├── model_dtcm_boot.bin
│   ├── model_ptcm_boot.bin
│   ├── model_sram0_boot.bin
│   └── model_info.json
├── Face_Recognition/
│   └── ...
└── ...
```

## Version Management

Version information is stored in `model_info.json` within each model directory.

**Example `model_info.json`:**
```json
{
    "name": "Face Detection",
    "version": "1.0.0",
    "date": "2024-01-01",
    "description": "RetinaFace based model."
}
```

The build system (CMake) automatically scans these directories and generates debug targets (e.g., `dbg_mm_test_face-detection`) displaying the version information.
