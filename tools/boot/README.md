# boot splicing recipe (ci16 head + newer payload)

`ci16-head.bin` is the first 0x16C000 bytes of `boot-ci16-adt.img` (the image
ABL accepts). Splice a freshly built boot.img onto it:

    python splice.py ci16-head.bin new-boot.img out.img

    out = head(0x16C000 bytes) + new[0x168000:]

Why: the raw boot.img produced by `build bootimg` is rejected by ABL with
`Decompress Failed` / `Failed to Decompress FVI Stat = Not Found`; only the
ci16 header passes. The new image carries kernel_size 0x1662BC and payload at
0x168000, the ci16 one 0x16AB55 at 0x16C000, both payloads 0xB001000 bytes.

Verified results:
- build #4 (9b49f64): out 186,044,416 B, sha256 6fef5b84...fc84 -> kernel panics
- build #5 (ac54156): out 186,044,416 B, sha256 D5611D93...2414 -> kernel panics

ci16-head.bin: 1,490,944 B, sha256 1015B16DD42960746B65B6B6B373D917834D66F404434474EE7294DE69DD539F