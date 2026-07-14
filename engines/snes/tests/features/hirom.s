; HiROM + CPU-register test ROM — verifies HiROM mapping (banks C0+, header
; at $FFC0), the multiply/divide registers ($4202-$4217), the WRAM data port
; ($2180-$2183), and the H/V IRQ timer ($4207-$420A + $4211). The screen is
; the backdrop colour only; every 32 frames it shows the next result word as
; a colour, so a snes9x lockstep frame-compare checks all the values:
;   0 marker word read long from $C00000      4 WRAM-port readback
;   1 $1D * $2F                               5 IRQ counter (1 per frame)
;   2 $C0DE / $1D quotient                    6 divide-by-zero quotient
;   3 $C0DE % $1D remainder                   7 word read from $C0FFF0
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
irqcnt: .res 1
slots:  .res 16          ; 8 result words

.segment "MARKER"
    .word $1F7C          ; slot 0 marker at file offset 0 = $C00000
    .byte "HIROM MARKER"

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

    ; slot 0: long read from bank $C0 (HiROM low half of the file).
    rep #$20
    lda f:$C00000
    sta slots+0
    sep #$20

    ; slot 1: $1D * $2F = $0553.
    lda #$1d
    sta $4202
    lda #$2f
    sta $4203
    nop
    nop
    nop
    nop
    rep #$20
    lda $4216
    sta slots+2
    sep #$20

    ; slots 2/3: $C0DE / $1D -> quotient $06A6, remainder $0A.
    rep #$20
    lda #$c0de
    sta $4204
    sep #$20
    lda #$1d
    sta $4206
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rep #$20
    lda $4214
    sta slots+4
    lda $4216
    sta slots+6
    sep #$20

    ; slot 4: WRAM data port: write $5A,$3C at $7E1234, read back via port.
    lda #$34
    sta $2181
    lda #$12
    sta $2182
    stz $2183
    lda #$5a
    sta $2180
    lda #$3c
    sta $2180
    lda #$34
    sta $2181
    lda #$12
    sta $2182
    stz $2183
    lda $2180
    sta slots+8
    lda $2180
    sta slots+9

    ; slot 6: divide by zero -> quotient $FFFF.
    rep #$20
    lda #$beef
    sta $4204
    sep #$20
    stz $4206
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rep #$20
    lda $4214
    sta slots+12
    sep #$20

    ; slot 7: long read near the top of bank $C0 (the header area).
    rep #$20
    lda f:$C0FFDC
    sta slots+14
    sep #$20

    ; H+V IRQ at (H=100, V=100): once per frame into irqcnt (slot 5).
    lda #100
    sta $4207
    stz $4208
    lda #100
    sta $4209
    stz $420a

    stz $212c            ; nothing on the main screen: backdrop only
    stz $212d
    lda #$0f
    sta $2100
    lda #$b0             ; NMI + H/V IRQ enable
    sta $4200
    cli

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

    ; CGRAM entry 0 = slots[(frame>>5)&7]; slot 5 shows the live IRQ count.
    lda frame
    lsr
    lsr
    lsr
    lsr
    lsr
    and #$07
    cmp #$05
    beq @irqslot
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
    bra @done
@irqslot:
    ; irqcnt-frame is a small constant when the timer fires once per frame,
    ; so the colour is init-skew-proof (a dead IRQ shows up loudly).
    stz $2121
    lda irqcnt
    sec
    sbc frame
    clc
    adc #$10
    sta $2122
    stz $2122
@done:
    rep #$20
    pla
    sep #$20
    rti

irq:
    rep #$20
    pha
    sep #$20
    lda $4211            ; ack TIMEUP
    inc irqcnt
    rep #$20
    pla
    sep #$20
    rti

stub:
    rti

.segment "SNESHEADER"
    .byte "HIROM REGS TEST      "   ; 21-char title
    .byte $21                        ; HiROM, slow
    .byte $00                        ; ROM only
    .byte $06                        ; 64 KB
    .byte $00
    .byte $01                        ; NTSC
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, irq, stub, stub
    .word stub, stub, stub, stub, reset, stub
