# Regenerating the SPC7110 decompressor goldens

`snes_spc7110_test.cpp` compares our `Spc7110Decomp` port against golden
bytes captured from byuu & neviksti's original reverse-engineered decoder
(`spc7110dec.cpp`, v0.03, ISC-style licence). To regenerate after the
reference or the fake-ROM LCG changes:

1. Fetch the reference: `spc7110dec.cpp` from a snes9x checkout.
2. Provide the class declaration + a stub `memory_cartrom_read/size` backed by
   the SAME deterministic LCG the test uses (`x = x*1103515245 + 12345`,
   byte = `x >> 16`, ROM size `0x600000`).
3. `d.init(mode,0,0); for 64: print d.read();` for modes 0/1/2.
4. Paste the three arrays into `kGold[3][64]` in the test.

The point of the gate: both decoders read an identical byte stream, so any
divergence is a port transcription bug, not an algorithm difference.
