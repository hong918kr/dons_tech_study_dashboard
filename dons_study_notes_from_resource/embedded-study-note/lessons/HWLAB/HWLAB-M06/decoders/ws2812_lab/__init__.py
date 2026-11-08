'''
WS2812/WS2812B RGB LED chain decoder with timing checks (HWLAB-M06).

Single-wire NRZ: a bit is a high pulse (T0H ~0.4 us = 0, T1H ~0.8 us = 1) in a ~1.25 us
period; 24 bits per LED (GRB, MSB first); a low longer than the reset time latches the chain.
'''

from .pd import Decoder
