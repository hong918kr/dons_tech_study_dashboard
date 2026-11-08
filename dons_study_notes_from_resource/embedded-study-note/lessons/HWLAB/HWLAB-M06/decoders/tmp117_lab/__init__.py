'''
TI TMP117 temperature sensor register decoder, stacked on I2C (HWLAB-M06).

Pointer write + 16-bit register read/write; Temp_Result is shown in degrees C (1 LSB = 1/128 C).
'''

from .pd import Decoder
