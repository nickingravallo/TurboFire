default: build

build: gto-solver

gto-solver:
	mkdir -p output
	gcc -O3 -Iinclude -lm -o output/TurboFire src/TurboFire.c src/MCCFR.c src/RangeParser.c src/GUI.c

gto-solver-gui:
	mkdir -p output
	@if pkg-config --exists sdl2 SDL2_ttf; then \
		gcc -O3 -Iinclude -DUSE_GUI -lm `pkg-config --cflags --libs sdl2 SDL2_ttf` -o output/TurboFire src/TurboFire.c src/MCCFR.c src/RangeParser.c src/GUI.c; \
		echo "Built with GUI support"; \
	else \
		echo "SDL2 not found. Install with: sudo apt-get install libsdl2-dev libsdl2-ttf-dev (Ubuntu/Debian)"; \
		echo "or: brew install sdl2 sdl2_ttf (macOS)"; \
		echo "Building without GUI support..."; \
		gcc -O3 -Iinclude -lm -o output/TurboFire src/TurboFire.c src/MCCFR.c src/RangeParser.c src/GUI.c; \
	fi

handrank-gen:
	mkdir -p output
	gcc -O3 -o output/HandRankGen src/HandRankGen.c

generate-handranks: handrank-gen
	output/HandRankGen
	@if [ -f handranks.dat ]; then mv handranks.dat output/; fi
	@echo "Generated output/handranks.dat"

simulator:
	mkdir -p output
	gcc -O3 -Iinclude -o output/Simulator src/Simulator.c

simulate: simulator
	output/Simulator

clean:
	rm -rf output
