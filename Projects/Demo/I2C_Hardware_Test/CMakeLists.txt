# I2C Hardware Test Demo
s300_add_executable(
    TARGET s300_i2c_hardware_test
    SOURCES
    Src/main.c
    DRIVERS
    uart
    i2c
    DBG_NAME i2c_hw
)
