; DSP-1 test ROM — drives the HLE command port (DR at $21:8000, SR at
; $21:C000) and shows result words as backdrop colours in 32-frame slots:
;   0 Multiply $1234*$0567>>15      4 Inverse exponent
;   1 Triangle sin($2000)*$4000     5 Radius high word (300,400,500)
;   2 Triangle cos($2000)*$4000     6 Range high word (300,400,500,450)
;   3 Inverse($0400) mantissa       7 Rotate2D X' ($1000, 1000, 2000)
.setcpu "65816"
.smart +

DSPDR = $218000
DSPSR = $21C000

.segment "ZEROPAGE"
frame:  .res 2
slots:  .res 16

.segment "CODE"

; ---- helpers ---------------------------------------------------------------
; write A to DR
dsp_wr:
    sta f:DSPDR
    rts
; read DR -> A
dsp_rd:
    lda f:DSPDR
    rts
; write word in X (lo/hi) to DR
dsp_wrw:
    rep #$20
    txa
    sep #$20
    sta f:DSPDR
    rep #$20
    txa
    xba
    sep #$20
    sta f:DSPDR
    rts
; read word from DR -> X
dsp_rdw:
    lda f:DSPDR
    sta tmpl
    lda f:DSPDR
    sta tmph
    ldx tmpl
    rts

.segment "ZEROPAGE"
tmpl:   .res 1
tmph:   .res 1

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

    ; ---- Multiply: cmd $00, in $1234, $0567 -> slot 0
    lda #$00
    jsr dsp_wr
    ldx #$1234
    jsr dsp_wrw
    ldx #$0567
    jsr dsp_wrw
    jsr dsp_rdw
    stx slots+0

    ; ---- Triangle: cmd $04, in angle $2000, radius $4000 -> slots 1, 2
    lda #$04
    jsr dsp_wr
    ldx #$2000
    jsr dsp_wrw
    ldx #$4000
    jsr dsp_wrw
    jsr dsp_rdw
    stx slots+2
    jsr dsp_rdw
    stx slots+4

    ; ---- Inverse: cmd $10, in $0400, exp 0 -> slots 3, 4
    lda #$10
    jsr dsp_wr
    ldx #$0400
    jsr dsp_wrw
    ldx #$0000
    jsr dsp_wrw
    jsr dsp_rdw
    stx slots+6
    jsr dsp_rdw
    stx slots+8

    ; ---- Radius: cmd $08, in 300,400,500 -> low, high; keep high -> slot 5
    lda #$08
    jsr dsp_wr
    ldx #300
    jsr dsp_wrw
    ldx #400
    jsr dsp_wrw
    ldx #500
    jsr dsp_wrw
    jsr dsp_rdw          ; low word (discard)
    jsr dsp_rdw
    stx slots+10

    ; ---- Range: cmd $18, in 300,400,500,450 -> slot 6
    lda #$18
    jsr dsp_wr
    ldx #300
    jsr dsp_wrw
    ldx #400
    jsr dsp_wrw
    ldx #500
    jsr dsp_wrw
    ldx #450
    jsr dsp_wrw
    jsr dsp_rdw
    stx slots+12

    ; ---- Rotate 2D: cmd $0C, angle $1000, X 1000, Y 2000 -> X' -> slot 7
    lda #$0c
    jsr dsp_wr
    ldx #$1000
    jsr dsp_wrw
    ldx #1000
    jsr dsp_wrw
    ldx #2000
    jsr dsp_wrw
    jsr dsp_rdw
    stx slots+14
    jsr dsp_rdw          ; Y' (discard)

    stz $212c
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
    lda frame
    lsr
    lsr
    lsr
    lsr
    lsr
    and #$07
    rep #$30
    and #$0007
    asl
    tax
    sep #$20
    stz $2121
    lda slots,x
    sta $2122
    lda slots+1,x
    and #$7f
    sta $2122
    rep #$20
    pla
    sep #$20
    rti

stub:
    rti

.segment "SNESHEADER"
    .byte "DSP1 HLE TEST        "
    .byte $20                        ; LoROM, slow -> DSP1 per NSRT rules
    .byte $03                        ; chip: DSP
    .byte $05                        ; 32 KB
    .byte $00
    .byte $01
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, stub, stub, stub
    .word stub, stub, stub, stub, reset, stub
