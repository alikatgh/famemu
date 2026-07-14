; OBJ limit probe — 20 non-overlapping 32x32 sprites (80 slivers) on one row:
; far more than the 34-sliver budget. Which ones snes9x draws settles the
; hardware drop order for famemu's evaluator. Distinct palettes per sprite.
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
tx:     .res 1
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

    ; OBJ 4bpp tiles 0-15 at word $0000: solid colour, bright border.
    lda #$80
    sta $2115
    ldx #$0000
    stx $2116
    stz tx
@t: stz row
@r: lda row
    beq @b
    lda tx
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2118
    lda tx
    and #$02
    beq :+
    lda #$7f
:   ora #$80
    sta $2119
    bra @n
@b: lda #$ff
    sta $2118
    sta $2119
@n: inc row
    lda row
    cmp #8
    bne @r
    stz row
@r2: lda row
    beq @b2
    lda tx
    and #$04
    beq :+
    lda #$7f
:   ora #$80
    sta $2118
    lda tx
    and #$08
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
    inc tx
    lda tx
    cmp #16
    bne @t

    ; OAM: sprite i (0-19): x = i*12, y = 100, tile = i&15, pal = i&7.
    stz $2102
    stz $2103
    stz tx
@oam:
    lda tx
    cmp #20
    bcs @park
    lda tx
    asl
    clc
    adc tx
    asl
    asl                  ; x = i*12
    sta $2104
    lda #100
    sta $2104
    lda tx
    and #$0f
    sta $2104
    lda tx
    and #$07
    asl
    sta $2104
    bra @next
@park:
    stz $2104
    lda #240
    sta $2104
    stz $2104
    stz $2104
@next:
    inc tx
    lda tx
    cmp #128
    bne @oam
    stz tx
@hi: lda #$aa             ; all large (32x32)
    sta $2104
    inc tx
    lda tx
    cmp #32
    bne @hi

    lda #$60             ; OBSEL: chars at $0000, sizes 16/32
    sta $2101
    lda #$01
    sta $2105
    lda #$10             ; OBJ only on main
    sta $212c
    stz $212d
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
    rep #$20
    pla
    sep #$20
    rti

stub:
    rti

.segment "SNESHEADER"
    .byte "OBJ LIMIT PROBE      "
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
