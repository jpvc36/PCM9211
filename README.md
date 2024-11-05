# PCM9211

Working Linux-6.x driver for the PCM9211. Dts file for the Raspberry Pi with a TAS582x added. This makes a soundcard with configurable inputs. See TI documentation for register descriptions.
A tested diagram for a Costas Loop DPSK demodulator is added. It takes a 96kHz SPDIF signal, phase modulated with 24.576 MHz and regenerates the SPDIF signal. Polarity is then taken care of by the SPDIF demodulator, eg. DIR9001 or PCM9211.
