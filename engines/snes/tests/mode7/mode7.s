; Mode 7 test ROM — exercises the S-PPU rotation/scaling path for the famemu
; SNES engine (gate: frame dumps identical to snes9x, tests/run_mode7_gate.sh).
;
; VRAM gets a 128x128 tilemap of 8x8-tile blocks (64 distinct tiles, each a
; solid CGRAM ramp colour with a bright top/left border line, so both tile
; identity and orientation survive rotation). Every 64 frames the NMI loads
; the next matrix case:
;   0 identity                        3 zoom-out 0.5x, transparent outside
;   1 rotate ~30 deg about (512,512)  4 zoom-out 0.5x, tile-0 fill outside
;   2 zoom-in 2x, negative scroll     5 identity, H+V screen flip
.setcpu "65816"
.smart +                 ; track rep/sep so immediate/index operand sizes match

.segment "ZEROPAGE"
frame:  .res 2
tx:     .res 1
ty:     .res 1
tile:   .res 1
row:    .res 1
col:    .res 1

.segment "CODE"

reset:
    sei
    clc
    xce                  ; native 65816
    rep #$38
    ldx #$1fff
    txs
    lda #$0000
    tcd
    sep #$20             ; A 8-bit, X/Y 16-bit

    ldx #$0000           ; clear low RAM
:   stz a:$0000,x
    inx
    cpx #$2000
    bne :-

    phk
    plb

    lda #$80             ; force blank while we upload
    sta $2100
    lda #$07             ; Mode 7
    sta $2105
    lda #$01             ; main screen: BG1 only
    sta $212c
    stz $212d
    stz $211a            ; M7SEL: wrap
    stz $2121            ; CGADD = 0

    ; CGRAM: colour i = word (i | ((i>>1)&$7F) << 8) — a deterministic ramp
    ; that hits white at $FF (border lines) and black at 0.
    ldx #$0000
@cg: txa
    sta $2122            ; low byte = i
    txa
    lsr
    and #$7f
    sta $2122            ; high byte = (i>>1)&$7F
    inx
    cpx #$0100
    bne @cg

    ; Tilemap (VRAM low bytes, word addrs 0-16383): tile = (ty>>4)*8 + (tx>>4)
    ; — an 8x8 grid of 16x16-tile blocks, tiles 0-63.
    stz $2115            ; VMAIN: inc after $2118 (low byte)
    ldx #$0000
    stx $2116
    stz ty
@tmy:
    stz tx
@tmx:
    lda ty
    lsr
    lsr
    lsr
    lsr
    asl
    asl
    asl                  ; (ty>>4)*8
    sta tile
    lda tx
    lsr
    lsr
    lsr
    lsr
    clc
    adc tile
    sta $2118
    inc tx
    lda tx
    cmp #128
    bne @tmx
    inc ty
    lda ty
    cmp #128
    bne @tmy

    ; Char data (VRAM high bytes, word addr t*64 + row*8 + col): pixel =
    ; tile*4 into the ramp, with row-0/col-0 drawn bright ($FF) as a grid.
    lda #$80             ; VMAIN: inc after $2119 (high byte)
    sta $2115
    ldx #$0000
    stx $2116
    stz tile
@ct: stz row
@cr: stz col
@cc: lda row
    beq @grid
    lda col
    beq @grid
    lda tile
    asl
    asl                  ; tile*4 — spreads 64 tiles over the 256-colour ramp
    bra @put
@grid:
    lda #$ff
@put:
    sta $2119
    inc col
    lda col
    cmp #8
    bne @cc
    inc row
    lda row
    cmp #8
    bne @cr
    inc tile
    lda tile
    bne @ct              ; all 256 tiles (64 patterns + wrapped repeats)

    jsr load_case        ; case 0 before the screen turns on

    lda #$0f
    sta $2100            ; screen on, full brightness
    lda #$80
    sta $4200            ; NMI on

main:
    wai
    bra main

; ---------------------------------------------------------------------------
nmi:
    rep #$20
    pha
    sep #$20
    lda $4210            ; ack
    rep #$20
    inc frame
    sep #$20
    jsr load_case
    rep #$20
    pla
    sep #$20
    rti

; Load matrix case (frame>>6, clamped to 5) into the M7 registers.
; Table entry: A,B,C,D,CX,CY,HOFS,VOFS (words, low byte first), M7SEL, pad.
load_case:
    rep #$30             ; A/X/Y 16-bit for the index math
    lda frame
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr                  ; /64
    cmp #$0006
    bcc :+
    lda #$0005
:   sta tile             ; scratch (tile..row = 2 bytes)
    asl
    asl
    asl
    asl                  ; *16 (carry clear: case <= 5)
    adc tile
    adc tile             ; *18
    tay
    sep #$20             ; A back to 8-bit
    ldx #$211b           ; write-twice regs $211B-$2120: A,B,C,D,CX,CY
@wt: lda cases,y
    sta a:$0000,x
    iny
    lda cases,y
    sta a:$0000,x
    iny
    inx
    cpx #$2121
    bne @wt
    lda cases,y          ; M7HOFS (write-twice, m7 latch)
    sta $210d
    iny
    lda cases,y
    sta $210d
    iny
    lda cases,y          ; M7VOFS
    sta $210e
    iny
    lda cases,y
    sta $210e
    iny
    lda cases,y          ; M7SEL
    sta $211a
    rts

stub:
    rti

.segment "RODATA"
; A, B, C, D, CX, CY, HOFS, VOFS (little-endian words), M7SEL, pad
cases:
    ; 0: identity at origin
    .word $0100, $0000, $0000, $0100, $0000, $0000, $0000, $0000
    .byte $00, $00
    ; 1: rotate ~30deg about (512,512), view over the centre
    .word $00de, $ff80, $0080, $00de, $0200, $0200, $0180, $01b0
    .byte $00, $00
    ; 2: zoom-in 2x, negative 13-bit scroll (clip() sign path)
    .word $0200, $0000, $0000, $0200, $0000, $0000, $1f00, $1f80
    .byte $00, $00
    ; 3: zoom-out 0.5x, off-map shows transparent (M7SEL=$80)
    .word $0080, $0000, $0000, $0080, $0000, $0000, $1e00, $1e00
    .byte $80, $00
    ; 4: zoom-out 0.5x, off-map fills with tile 0 (M7SEL=$C0)
    .word $0080, $0000, $0000, $0080, $0000, $0000, $1e00, $1e00
    .byte $c0, $00
    ; 5: identity, H+V screen flip (M7SEL=$03)
    .word $0100, $0000, $0000, $0100, $0000, $0000, $0000, $0000
    .byte $03, $00

.segment "SNESHEADER"
    .byte "MODE7 ENGINE TEST    "   ; 21-char title
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
