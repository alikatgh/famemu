; Hires test ROM — true 512-wide output. 64-frame cases:
;   0 Mode 5 (BG1 4bpp main + BG2 2bpp sub, weave)
;   1 Mode 1 pseudo-hires (SETINI bit3, BG2 on the subscreen)
;   2 Mode 5 + interlace (SETINI bit1) — golden-only (snes9x emits 448 rows)
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
tx:     .res 1
ty:     .res 1
tile:   .res 1
row:    .res 1

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
    sta $2100
    stz $2121
    ldx #$0000
@cg: txa
    sta $2122
    txa
    lsr
    and #$7f
    sta $2122
    inx
    cpx #$0100
    bne @cg

    ; 4bpp tiles 0-15 at $0000 (solid colour, bright border row/col).
    lda #$80
    sta $2115
    ldx #$0000
    stx $2116
    stz tile
@t4: stz row
@r4: lda row
    beq @b4
    lda tile
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2118
    lda tile
    and #$02
    beq :+
    lda #$7f
:   ora #$80
    sta $2119
    bra @n4
@b4: lda #$ff
    sta $2118
    sta $2119
@n4: inc row
    lda row
    cmp #8
    bne @r4
    stz row
@r4b: lda row
    beq @b4b
    lda tile
    and #$04
    beq :+
    lda #$7f
:   ora #$80
    sta $2118
    lda tile
    and #$08
    beq :+
    lda #$7f
:   ora #$80
    sta $2119
    bra @n4b
@b4b: lda #$ff
    sta $2118
    sta $2119
@n4b: inc row
    lda row
    cmp #8
    bne @r4b
    inc tile
    lda tile
    cmp #16
    bne @t4

    ; 2bpp tiles 0-15 at $2000.
    ldx #$2000
    stx $2116
    stz tile
@t2: stz row
@r2: lda row
    beq @b2
    lda tile
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2118
    lda tile
    and #$02
    beq :+
    lda #$7f
:   ora #$80
    sta $2119
    bra @n2
@b2: lda #$ff
    sta $2118
    sta $2119
@n2: inc row
    lda row
    cmp #8
    bne @r2
    inc tile
    lda tile
    cmp #16
    bne @t2

    ; BG1 map at $1000: tile=(tx+ty)&15, pal=(tx>>2)&7, some flips/prio.
    ldx #$1000
    stx $2116
    stz ty
@m1y: stz tx
@m1x:
    lda tx
    clc
    adc ty
    and #$0f
    sta $2118
    lda tx
    lsr
    lsr
    and #$07
    asl
    asl
    sta row              ; scratch: pal bits in high byte
    lda tx
    and #$02
    beq :+
    lda #$40
    ora row
    sta row
:   lda row
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m1x
    inc ty
    lda ty
    cmp #32
    bne @m1y

    ; BG2 map at $1400: tile=(tx*3+ty)&15, pal 1.
    ldx #$1400
    stx $2116
    stz ty
@m2y: stz tx
@m2x:
    lda tx
    asl
    clc
    adc tx
    clc
    adc ty
    and #$0f
    sta $2118
    lda #$04
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m2x
    inc ty
    lda ty
    cmp #32
    bne @m2y

    jsr load_case

    lda #$0f
    sta $2100
    lda #$80
    sta $4200

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
    sep #$20
    jsr load_case
    rep #$20
    pla
    sep #$20
    rti

load_case:
    lda #$10             ; BG1 map $1000
    sta $2107
    lda #$14             ; BG2 map $1400
    sta $2108
    lda #$20             ; BG1 chars $0000, BG2 chars $2000
    sta $210b
    lda #$04             ; BG1H = 4 (odd fine scroll exercises subpixels)
    sta $210d
    stz $210d
    lda #$02
    sta $210e
    stz $210e
    lda #$06             ; BG2H = 6
    sta $210f
    stz $210f
    stz $2110
    stz $2110
    stz $2133

    rep #$30
    lda frame
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr
    cmp #$0003
    bcc :+
    lda #$0002
:   asl
    tax
    sep #$20
    jmp (case_tbl,x)

case0:                   ; Mode 5
    lda #$05
    sta $2105
    lda #$01             ; main: BG1
    sta $212c
    lda #$02             ; sub: BG2
    sta $212d
    rts

case1:                   ; Mode 1 pseudo-hires
    lda #$01
    sta $2105
    lda #$01
    sta $212c
    lda #$02
    sta $212d
    lda #$08             ; SETINI: pseudo-hires
    sta $2133
    rts

case2:                   ; Mode 5 + BG interlace (golden-only)
    lda #$05
    sta $2105
    lda #$01
    sta $212c
    lda #$02
    sta $212d
    lda #$02             ; SETINI: BG interlace
    sta $2133
    rts

stub:
    rti

.segment "RODATA"
case_tbl:
    .word case0, case1, case2

.segment "SNESHEADER"
    .byte "HIRES WEAVE TEST     "
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
