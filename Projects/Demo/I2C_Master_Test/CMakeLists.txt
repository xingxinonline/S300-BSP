# I2C Master Test Demo
s300_add_executable(
    TARGET s300_i2c_master_test
    SOURCES
    Src/main.c
    DRIVERS
    uart
    i2c
)