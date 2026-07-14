; Raster-split test ROM — an H-IRQ at H=160 on every scanline toggles the
; backdrop colour (red/blue) MID-LINE, so each row shows the previous colour
; on the left of the split and the new one on the right. Exercises famemu's
; dot-level catch-up rendering; golden-CRC gated (snes9x renders whole lines,
; so this ROM stays out of the lockstep suite by design).
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
tog:    .res 1

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
    stz $212c            ; backdrop only
    stz $212d

    lda #160             ; H-IRQ at dot 160, every line
    sta $4207
    stz $4208

    lda #$0f
    sta $2100
    lda #$90             ; NMI + H-IRQ
    sta $4200
    cli

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

irq:
    rep #$20
    pha
    sep #$20
    lda $4211            ; ack TIMEUP
    lda tog
    eor #$01
    sta tog
    stz $2121
    beq @blue
    lda #$1f             ; red ($001F)
    sta $2122
    stz $2122
    bra @done
@blue:
    stz $2122
    lda #$7c             ; blue ($7C00)
    sta $2122
@done:
    rep #$20
    pla
    sep #$20
    rti

stub:
    rti

.segment "SNESHEADER"
    .byte "RASTER SPLIT TEST    "
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
    .word stub, stub, stub, nmi, stub, irq, stub, stub
    .word stub, stub, stub, stub, reset, stub
