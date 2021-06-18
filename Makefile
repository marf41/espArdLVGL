serial:
	platformio run -e serial -t upload

monitor:
	pio device monitor -e serial
