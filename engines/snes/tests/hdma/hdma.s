; HDMA test ROM — exercises the famemu SNES engine's per-scanline DMA
; (gate: frame dumps identical to snes9x).
;
; BG1 (mode 1, 4bpp) shows an 8x8-px checkerboard. Two HDMA channels run:
;   ch0: mode 0 direct -> $2132 COLDATA, an additive blue sky gradient in
;        8-line bands (28 entries — exercises entry reload + direct advance)
;   ch1: mode 2 repeat -> $210D BG1HOFS (write-twice), a zigzag wave that
;        re-writes the scroll EVERY line (exercises the repeat flag)
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2

.segment "CODE"

reset:
    sei
    clc
    xce
    rep #$38
    ldx #$1fff
    txs
    lda #$0000
    tcd
    sep #$20

    ldx #$0000
:   stz a:$0000,x
    inx
    cpx #$2000
    bne :-

    phk
    plb

    lda #$80
    sta $2100            ; force blank
    lda #$01
    sta $2105            ; mode 1
    lda #$01
    sta $212c            ; main screen: BG1
    stz $212d
    stz $2107            ; BG1 map @ word $0000... keep tiles above
    lda #$01
    sta $210b            ; BG1 tiles @ word $1000
    stz $210d            ; BG1HOFS = 0 (write twice)
    stz $210d
    stz $210e
    stz $210e

    ; palette: 0 backdrop deep blue; pal0 idx1 green, idx2 dark green
    stz $2121
    lda #$40             ; BGR555 lo of (0,2,8)-ish deep blue
    sta $2122
    lda #$28
    sta $2122
    lda #$e0             ; green
    sta $2122
    lda #$02
    sta $2122
    lda #$a0             ; darker green
    sta $2122
    lda #$01
    sta $2122

    ; two solid 4bpp tiles at word $1000: tile 0 = color 1, tile 1 = color 2
    lda #$80             ; inc after $2119 (we write both bytes per word)
    sta $2115
    ldx #$1000
    stx $2116
    ldx #$0000           ; tile 0: plane0 = FF x8 (color 1)
:   lda #$ff
    sta $2118
    stz $2119
    inx
    cpx #$0008
    bne :-
    ldx #$0000           ; planes 2/3 zero
:   stz $2118
    stz $2119
    inx
    cpx #$0008
    bne :-
    ldx #$0000           ; tile 1: plane1 = FF (color 2)
:   stz $2118
    lda #$ff
    sta $2119
    inx
    cpx #$0008
    bne :-
    ldx #$0000
:   stz $2118
    stz $2119
    inx
    cpx #$0008
    bne :-

    ; tilemap at word $0000: checkerboard of tiles 0/1
    lda #$80             ; inc after $2119
    sta $2115
    ldx #$0000
    stx $2116
    ldx #$0000
@map:
    ; tile = (col + row) & 1: col = X & 31, row = X >> 5
    rep #$20
    txa
    and #$001f
    sta a:$0004          ; scratch (above the frame counter)
    txa
    lsr
    lsr
    lsr
    lsr
    lsr
    clc
    adc a:$0004
    sep #$20
    and #$01
    sta $2118
    stz $2119
    inx
    cpx #$0400
    bne @map

    ; ---- HDMA tables -----------------------------------------------------
    ; ch0: mode 0 direct, B-bus $32 (COLDATA)
    stz $4300
    lda #$32
    sta $4301
    ldx #.loword(grad_tab)
    stx $4302
    lda #^grad_tab
    sta $4304
    ; ch1: mode 2 (2 bytes, same reg), B-bus $0D (BG1HOFS write-twice)
    lda #$02
    sta $4310
    lda #$0d
    sta $4311
    ldx #.loword(wave_tab)
    stx $4312
    lda #^wave_tab
    sta $4314
    lda #$03
    sta $420c            ; HDMA channels 0+1 on

    ; color math: ADD the fixed colour to BG1 + backdrop
    stz $2130
    lda #$21
    sta $2131

    lda #$0f
    sta $2100            ; screen on
    lda #$80
    sta $4200            ; NMI on

main:
    wai
    bra main

nmi:
    rep #$20
    pha
    sep #$20
    lda $4210
    rep #$20
    inc frame
    pla
    sep #$20
    rti

stub:
    rti

.segment "RODATA"
; sky gradient: 28 bands x 8 lines, blue intensity 27 -> 0 (adds over scene)
grad_tab:
.repeat 28, i
    .byte 8, $80 | (27 - i)
.endrepeat
    .byte 0
; zigzag wave: repeat blocks re-write BG1HOFS (lo,hi) every line
wave_tab:
.repeat 14, blk
    .byte $80 | 16
    .repeat 8, i
        .byte i, 0, i, 0
    .endrepeat
.endrepeat
    .byte 0

.segment "SNESHEADER"
    .byte "HDMA ENGINE TEST     "
    .byte $20
    .byte $00
    .byte $05
    .byte $00
    .byte $01
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, stub, stub, stub
    .word stub, stub, stub, stub, reset, stub
