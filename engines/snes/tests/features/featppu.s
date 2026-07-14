; PPU feature test ROM — exercises the general-purpose S-PPU paths of the
; famemu SNES engine (gate: frame dumps identical to snes9x via
; tests/verify_features.sh). Every 64 frames the NMI switches cases:
;   0 Mode 1 baseline (BG1+BG2+OBJ, scrolled)   5 Mode 3 + direct color
;   1 windows (W1/W2, XOR logic, OBJ+math win)  6 Mode 2 offset-per-tile
;   2 mosaic 8x8 on BG1+BG2                     7 Mode 1, 16x16 BG1 tiles
;   3 Mode 0 (4 BGs, per-BG palette offsets)    8 OAM stress + prio rotation
;   4 Mode 3 (8bpp BG1)                         9 color math + clip window
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
tx:     .res 1
ty:     .res 1
tile:   .res 1
row:    .res 1
col:    .res 1
tmpw:   .res 2

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
    stz $2121

    ; CGRAM ramp: colour i = word (i | ((i>>1)&$7F) << 8).
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

    ; ---- 4bpp chars, tiles 0-15 at word $0000: solid colour t with a bright
    ; top row / left column border.
    lda #$80
    sta $2115            ; VMAIN: word writes, inc after $2119
    ldx #$0000
    stx $2116
    stz tile
@t4: stz row             ; planes 0/1, rows 0-7
@r4: lda row
    beq @b4
    lda tile
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2118            ; plane 0 (col0 border bit high)
    lda tile
    and #$02
    beq :+
    lda #$7f
:   ora #$80
    sta $2119            ; plane 1
    bra @n4
@b4: lda #$ff
    sta $2118
    sta $2119
@n4: inc row
    lda row
    cmp #8
    bne @r4
    stz row              ; planes 2/3, rows 0-7
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

    ; ---- 2bpp chars, tiles 0-15 at word $2000 (colour t&3, border row).
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

    ; ---- 8bpp chars, tiles 0-15 at word $4000: solid value t*17, border 255.
    ldx #$4000
    stx $2116
    stz tile
@t8: stz col             ; col reused as plane-pair index 0-3
@p8: stz row
@r8: lda row
    beq @b8
    lda tile
    asl
    asl
    asl
    asl                  ; t*16
    clc
    adc tile             ; t*17
    sta tmpw
    lda col
    asl                  ; plane = pair*2
    tay
    lda tmpw
@sh8: cpy #$00
    beq @done8
    lsr
    dey
    bra @sh8
@done8:
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2118            ; even plane
    lda col
    asl
    tay
    iny                  ; plane = pair*2 + 1
    lda tmpw
@sh8b: cpy #$00
    beq @done8b
    lsr
    dey
    bra @sh8b
@done8b:
    and #$01
    beq :+
    lda #$7f
:   ora #$80
    sta $2119            ; odd plane
    bra @n8
@b8: lda #$ff
    sta $2118
    sta $2119
@n8: inc row
    lda row
    cmp #8
    bne @r8
    inc col
    lda col
    cmp #4
    bne @p8
    inc tile
    lda tile
    cmp #16
    bne @t8

    ; ---- OBJ 4bpp chars, tiles 0-15 at word $6000 (same pattern as BG 4bpp).
    ldx #$6000
    stx $2116
    stz tile
@to: stz row
@ro: lda row
    beq @bo
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
    bra @no
@bo: lda #$ff
    sta $2118
    sta $2119
@no: inc row
    lda row
    cmp #8
    bne @ro
    stz row
@rob: lda row
    beq @bob
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
    bra @nob
@bob: lda #$ff
    sta $2118
    sta $2119
@nob: inc row
    lda row
    cmp #8
    bne @rob
    inc tile
    lda tile
    cmp #16
    bne @to

    ; ---- BG1 map at word $1000: tile=(tx+ty)&15, pal=(tx>>2)&7, flips,
    ; priority on tx&4.
    ldx #$1000
    stx $2116
    stz ty
@m1y: stz tx
@m1x:
    lda tx
    clc
    adc ty
    and #$0f
    sta tmpw             ; entry low: tile
    lda tx
    lsr
    lsr
    and #$07             ; pal
    asl
    asl                  ; pal<<2 (bits 2-4 of high byte)
    sta tmpw+1
    lda tx
    and #$04
    beq :+
    lda #$20             ; priority bit (bit13)
    ora tmpw+1
    sta tmpw+1
:   lda tx
    and #$01
    beq :+
    lda #$40             ; xflip
    ora tmpw+1
    sta tmpw+1
:   lda ty
    and #$01
    beq :+
    lda #$80             ; yflip
    ora tmpw+1
    sta tmpw+1
:   lda tmpw
    sta $2118
    lda tmpw+1
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m1x
    inc ty
    lda ty
    cmp #32
    bne @m1y

    ; ---- BG2 map at word $1400: tile=(tx*3+ty)&15, pal 2.
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
    lda #$08             ; pal 2 -> high byte bits: 2<<2
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m2x
    inc ty
    lda ty
    cmp #32
    bne @m2y

    ; ---- BG3 map at word $1800: tile=(tx^ty)&3, pal 1.
    ldx #$1800
    stx $2116
    stz ty
@m3y: stz tx
@m3x:
    lda tx
    eor ty
    and #$03
    sta $2118
    lda #$04
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m3x
    inc ty
    lda ty
    cmp #32
    bne @m3y

    ; ---- BG4 map at word $1C00. Rows 0/1 double as the offset-per-tile
    ; table for case 6 (BG3SC points here then): row 0 = H offsets with the
    ; apply bit alternating BG1/BG2, row 1 = V offsets applying to both.
    ldx #$1c00
    stx $2116
    stz tx
@opth:
    lda tx
    asl
    asl
    asl                  ; (tx*8) & $F8
    sta $2118
    lda tx
    lsr
    lsr
    lsr
    lsr
    lsr                  ; bit 8-9 of tx*8 = tx>>5 (tx<32 so 0-0)
    and #$03
    sta tmpw+1
    lda tx
    and #$01
    beq :+
    lda #$40             ; bit14: apply to BG2
    ora tmpw+1
    sta tmpw+1
    bra @wh
:   lda #$20             ; bit13: apply to BG1
    ora tmpw+1
    sta tmpw+1
@wh: lda tmpw+1
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @opth
    stz tx
@optv:
    lda tx
    asl
    asl                  ; tx*4
    sta $2118
    lda #$60             ; bits 13+14: apply V offset to BG1 and BG2
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @optv
    stz ty               ; rows 2-31: plain BG4 tiles
    inc ty
    inc ty
@m4y: stz tx
@m4x:
    lda ty
    asl
    clc
    adc tx
    and #$03
    sta $2118
    lda #$0c             ; pal 3
    sta $2119
    inc tx
    lda tx
    cmp #32
    bne @m4x
    inc ty
    lda ty
    cmp #32
    bne @m4y

    ; ---- OAM: 40 in-range sprites (rows near y=100), rest parked at y=240.
    stz $2102
    stz $2103
    stz tx
@oam:
    lda tx
    cmp #40
    bcs @park
    lda tx               ; x = (i*10) & $FF
    sta tmpw
    asl
    asl
    clc
    adc tmpw
    asl
    sta $2104
    lda tx
    and #$03
    asl
    asl
    asl                  ; (i&3)*8
    clc
    adc #100             ; y
    sta $2104
    lda tx
    and #$0f
    sta $2104            ; tile
    lda tx
    lsr
    lsr
    and #$03
    asl
    asl
    asl
    asl                  ; prio<<4
    sta tmpw+1
    lda tx
    and #$07
    asl                  ; pal<<1
    ora tmpw+1
    sta $2104            ; attr
    bra @noam
@park:
    stz $2104
    lda #240
    sta $2104
    stz $2104
    stz $2104
@noam:
    inc tx
    lda tx
    cmp #128
    bne @oam
    stz tx               ; high table: size large for odd sprites
@oamh:
    lda #$88             ; bits 3,7 = size of sprites 1,3 in each group
    sta $2104
    inc tx
    lda tx
    cmp #32
    bne @oamh

    lda #$63             ; OBSEL: OBJ chars at word $6000, sizes 16/32
    sta $2101

    jsr load_case

    lda #$0f
    sta $2100
    lda #$80
    sta $4200

main:
    wai
    bra main

; ---------------------------------------------------------------------------
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

; Reset the case-variable registers to the baseline, then apply frame>>6.
load_case:
    lda #$01
    sta $2105            ; Mode 1
    stz $210b            ; BG1/BG2 chars at $0000
    lda #$22
    sta $210c            ; BG3/BG4 chars at $2000
    lda #$10
    sta $2107            ; BG1 map $1000
    lda #$14
    sta $2108            ; BG2 map $1400
    lda #$18
    sta $2109            ; BG3 map $1800
    lda #$1c
    sta $210a            ; BG4 map $1C00
    lda #$13
    sta $212c            ; main: BG1+BG2+OBJ
    stz $212d            ; sub: none
    lda #$08             ; BG1H = 8
    sta $210d
    stz $210d
    lda #$04             ; BG1V = 4
    sta $210e
    stz $210e
    lda #$0c             ; BG2H = 12
    sta $210f
    stz $210f
    lda #$fa             ; BG2V = 250
    sta $2110
    stz $2110
    stz $2111
    stz $2111
    stz $2112
    stz $2112
    stz $2113
    stz $2113
    stz $2114
    stz $2114
    stz $2106            ; mosaic off
    stz $2123
    stz $2124
    stz $2125
    stz $2126
    stz $2127
    stz $2128
    stz $2129
    stz $212a
    stz $212b
    stz $212e
    stz $212f
    stz $2130            ; CGWSEL
    stz $2131            ; CGADSUB
    lda #$e0             ; COLDATA: all channels 0
    sta $2132
    stz $2133
    stz $2102            ; OAM address/rotation off
    stz $2103

    rep #$30             ; case = frame>>6, clamped to 9
    lda frame
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr
    cmp #$000a
    bcc :+
    lda #$0009
:   asl
    tax
    sep #$20
    jmp (case_tbl,x)

case0:                   ; baseline
    rts

case1:                   ; windows
    lda #$a2             ; BG1: W1 on. BG2: W1+W2 on.
    sta $2123
    lda #$82             ; OBJ: W1 on. MATH: W2 on.
    sta $2125
    lda #40
    sta $2126
    lda #120
    sta $2127
    lda #80
    sta $2128
    lda #200
    sta $2129
    lda #$08             ; BG2 window logic: XOR
    sta $212a
    lda #$13             ; TMW: mask BG1+BG2+OBJ in-window
    sta $212e
    rts

case2:                   ; mosaic
    lda #$73             ; size 8, BG1+BG2
    sta $2106
    rts

case3:                   ; Mode 0
    stz $2105
    lda #$1f
    sta $212c
    rts

case4:                   ; Mode 3 (8bpp BG1)
    lda #$03
    sta $2105
    lda #$04             ; BG1 chars at word $4000
    sta $210b
    rts

case5:                   ; Mode 3 + direct color
    lda #$03
    sta $2105
    lda #$04
    sta $210b
    lda #$01
    sta $2130
    rts

case6:                   ; Mode 2 offset-per-tile (OPT table in BG4 map rows)
    lda #$02
    sta $2105
    lda #$1c             ; BG3SC -> $1C00 (the OPT rows)
    sta $2109
    rts

case7:                   ; 16x16 BG1 tiles
    lda #$11
    sta $2105
    rts

case8:                   ; OAM stress + priority rotation
    lda #40              ; OAMADD word 40 -> first sprite 20
    sta $2102
    lda #$80
    sta $2103
    rts

case9:                   ; color math: clip inside math window, subtract
    lda #$11             ; main: BG1+OBJ
    sta $212c
    lda #$02             ; sub: BG2
    sta $212d
    lda #$20             ; MATH window: W1 on
    sta $2125
    lda #60
    sta $2126
    lda #180
    sta $2127
    lda #$82             ; clip inside window, math always, subscreen operand
    sta $2130
    lda #$e1             ; subtract, half, on BG1 + backdrop
    sta $2131
    lda #$2c             ; COLDATA fallback: red 12
    sta $2132
    lda #$46             ; green 6
    sta $2132
    lda #$94             ; blue 20
    sta $2132
    rts

stub:
    rti

.segment "RODATA"
case_tbl:
    .word case0, case1, case2, case3, case4
    .word case5, case6, case7, case8, case9

.segment "SNESHEADER"
    .byte "PPU FEATURES TEST    "   ; 21-char title
    .byte $20                        ; LoROM, slow
    .byte $00                        ; ROM only
    .byte $05                        ; 32 KB
    .byte $00
    .byte $01                        ; NTSC
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, stub, stub, stub
    .word stub, stub, stub, stub, reset, stub
