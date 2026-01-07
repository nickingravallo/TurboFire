default: build

build:
	mkdir -p output
	gcc -Iinclude -o output/TurboFire src/TurboFire.c src/Card.c src/Deck.c src/Game.c

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
