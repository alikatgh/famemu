; DSP-2 test ROM — drives the HLE command port and shows result words as
; backdrop colours in 32-frame slots:
;   0 Op09 multiply low word        2 Op06 reversed pair
;   1 Op09 multiply high word       3 Op01 bitplane bytes 0/1
;   4 Op0D scaled pair              5-7 zero
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

    ; ---- Op09 multiply: $1234 * $5678 -> 32-bit
    lda #$09
    jsr dsp_wr
    lda #$34
    jsr dsp_wr
    lda #$12
    jsr dsp_wr
    lda #$78
    jsr dsp_wr
    lda #$56
    jsr dsp_wr
    jsr dsp_rdw
    stx slots+0
    jsr dsp_rdw
    stx slots+2

    ; ---- Op06 reverse: len 2, bytes $AB, $CD
    lda #$06
    jsr dsp_wr
    lda #$02
    jsr dsp_wr
    lda #$ab
    jsr dsp_wr
    lda #$cd
    jsr dsp_wr
    jsr dsp_rdw
    stx slots+4

    ; ---- Op01 bitplane conversion: 32 bytes of a ramp -> first word
    lda #$01
    jsr dsp_wr
    ldx #$0000
@op1:
    rep #$20
    txa
    sep #$20
    eor #$5a
    sta f:DSPDR
    inx
    cpx #$0020
    bne @op1
    jsr dsp_rdw
    stx slots+6

    ; ---- Op0D scale: 8 pixels down to 4 -> first output pair
    lda #$0d
    jsr dsp_wr
    lda #$08             ; in len (pixels)
    jsr dsp_wr
    lda #$04             ; out len (pixels)
    jsr dsp_wr
    lda #$12
    jsr dsp_wr
    lda #$34
    jsr dsp_wr
    lda #$56
    jsr dsp_wr
    lda #$78
    jsr dsp_wr
    jsr dsp_rdw
    stx slots+8

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
    .byte "DSP2 HLE TEST        "
    .byte $20                        ; LoROM fast $20 -> DSP2 per NSRT rules
    .byte $05                        ; chip: DSP
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
