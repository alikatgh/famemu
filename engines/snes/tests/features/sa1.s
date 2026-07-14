; SA-1 test ROM — boots the SA-1 from the CRV vector, runs its arithmetic
; unit (multiply, divide), writes through IRAM, BW-RAM (bank $40 and the
; $6000 window) and the SCNT mailbox, then the S-CPU shows each result word
; as the backdrop colour in 32-frame slots (snes9x lockstep gate):
;   0 $1234*$0567 low word     4 BW-RAM word written via SA-1 bank $40
;   1 mul high word ($2308/9)  5 SFR message bits from SA-1's SCNT
;   2 $3039/$0025 quotient     6 BW-RAM word written via SA-1 $6000 window
;   3 remainder                7 IRAM sentinel $1F7C
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
slots:  .res 16

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

    ; SA-1 vectors + release from reset.
    lda #<sa1_entry
    sta $2203
    lda #>sa1_entry
    sta $2204
    lda #<sa1_spin
    sta $2205
    lda #>sa1_spin
    sta $2206
    lda #<sa1_spin
    sta $2207
    lda #>sa1_spin
    sta $2208
    stz $2200            ; CCNT: clear reset bit -> SA-1 boots at CRV

    ; Wait for the SA-1 to flag completion in IRAM.
@wait:
    lda $3000
    cmp #$a5
    bne @wait

    ; Collect result words.
    rep #$20
    lda $3001            ; slot 0: mul low
    sta slots+0
    lda $3003            ; slot 1: mul high
    sta slots+2
    lda $3005            ; slot 2: quotient
    sta slots+4
    lda $3007            ; slot 3: remainder
    sta slots+6
    lda f:$400100        ; slot 4: BW-RAM via bank $40
    sta slots+8
    sep #$20
    lda $2300            ; slot 5: SFR (IRQ flag + message nibble)
    sta slots+10
    stz slots+11
    lda #$80
    sta $2202            ; ack the SA-1 IRQ flag
    rep #$20
    lda f:$400040        ; slot 6: BW-RAM written via the SA-1 $6000 window
    sta slots+12
    lda $300a            ; slot 7: sentinel
    sta slots+14
    sep #$20

    stz $212c
    stz $212d
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

; ---------------------------------------------------------------------------
; SA-1 side. Runs from the same MMC-mapped ROM; results land in IRAM
; ($3000-$37FF from both CPUs) and BW-RAM.
sa1_entry:
    sei
    clc
    xce
    rep #$38
    ldx #$07ff           ; SA-1 stack in IRAM
    txs
    lda #$0000
    tcd
    sep #$20

    ; multiply $1234 * $0567 = $00_0625_C16C -> MR
    stz $2250
    lda #$34
    sta $2251
    lda #$12
    sta $2252
    lda #$67
    sta $2253
    lda #$05
    sta $2254
    rep #$20
    lda $2306
    sta $3001            ; mul low word
    lda $2308
    sta $3003            ; mul high word
    sep #$20

    ; divide $3039 / $0025: quotient $014D, remainder $0018
    lda #$01
    sta $2250
    lda #$39
    sta $2251
    lda #$30
    sta $2252
    lda #$25
    sta $2253
    stz $2254
    rep #$20
    lda $2306
    sta $3005            ; quotient
    lda $2308
    sta $3007            ; remainder
    sep #$20

    ; BW-RAM through bank $40 and through the $6000 window (block 0).
    lda #$77
    sta f:$400100
    lda #$88
    sta f:$400101
    lda #$c3
    sta $6040
    lda #$3c
    sta $6041

    ; IRAM sentinel, message + IRQ to the S-CPU, ready flag.
    lda #$7c
    sta $300a
    lda #$1f
    sta $300b
    lda #$8a             ; SCNT: IRQ + message $A
    sta $2209
    lda #$a5
    sta $3000
sa1_spin:
    bra sa1_spin

.segment "SNESHEADER"
    .byte "SA1 COPRO TEST       "   ; 21-char title
    .byte $23                        ; SA-1
    .byte $34                        ; chip: SA-1
    .byte $06                        ; 64 KB
    .byte $05                        ; 32 KB BW-RAM
    .byte $01                        ; NTSC
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, stub, stub, stub
    .word stub, stub, stub, stub, reset, stub

.segment "FILLER"
    .byte $5A
