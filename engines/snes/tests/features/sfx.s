; SuperFX test ROM — the GSU draws a 64x64 XOR texture into Game Pak RAM
; with COLOR/PLOT (16-colour mode) plus register-prefix ALU ops, delay-slot
; LOOP/branch flow, and STOP. The S-CPU polls GO from a WRAM stub (ROM is
; GSU-owned while it runs), then DMAs the bitmap to VRAM and shows it as
; BG1 tiles (snes9x lockstep gate at a late frame).
.setcpu "65816"
.smart +

.segment "ZEROPAGE"
frame:  .res 2
tx:     .res 1
ty:     .res 1

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

    ; CGRAM ramp (16-colour palette 0 comes from the same ramp).
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

    ; Copy the GO-poll stub to WRAM $0300 (ROM belongs to the GSU meanwhile).
    ldx #$0000
@cp: lda poll_stub,x
    sta a:$0300,x
    inx
    cpx #(poll_stub_end - poll_stub)
    bne @cp

    ; GSU setup: 16-colour bitmap at RAM $0000, 128px screen height,
    ; RAM+ROM granted to the GSU, IRQ masked.
    stz $3038            ; SCBR
    lda #$19             ; SCMR: MD=1 (16 col), HT=128, RAN+RON
    sta $303a
    stz $3039            ; CLSR: 10.7 MHz
    lda #$80             ; CFGR: mask the GSU IRQ
    sta $3037
    stz $3034            ; PBR = 0

    lda #<gsu_code       ; R15 write starts the GSU
    sta $301e
    lda #>gsu_code
    sta $301f

    jsr $0300            ; poll SFR.GO from WRAM until the GSU stops

    stz $303a            ; SCMR: hand RAM/ROM back to the S-CPU

    ; DMA the 2 KB bitmap (8x8 tiles, GSU column order) from bank $70 to
    ; VRAM $0000.
    lda #$80
    sta $2115
    ldx #$0000
    stx $2116
    lda #$01             ; mode 1: two registers ($2118/19)
    sta $4300
    lda #$18
    sta $4301
    stz $4302            ; A1T = $70:0000
    stz $4303
    lda #$70
    sta $4304
    stz $4305
    lda #$08             ; $0800 bytes
    sta $4306
    lda #$01
    sta $420b

    ; BG1 map at $1000: tile (tx*16+ty) at map cell (tx,ty) — the GSU bitmap
    ; is column-major (16 tiles per column at 128px height).
    ldx #$1000
    stx $2116
    stz ty
@my: stz tx
@mx: lda ty
    cmp #8
    bcs @blank
    lda tx
    cmp #8
    bcs @blank
    asl
    asl
    asl
    asl                  ; tx*16
    clc
    adc ty
    sta $2118
    stz $2119
    bra @nx
@blank:
    lda #$0f             ; solid tile 15 elsewhere (from the DMA'd garbage? no:
    sta $2118            ; tile 15 is bitmap data too — deterministic either way)
    stz $2119
@nx: inc tx
    lda tx
    cmp #32
    bne @mx
    inc ty
    lda ty
    cmp #32
    bne @my

    lda #$01             ; Mode 1, BG1 only
    sta $2105
    stz $210b
    lda #$10
    sta $2107
    lda #$01
    sta $212c
    stz $212d
    lda #$0f
    sta $2100
    lda #$80
    sta $4200

main:
    wai
    bra main

; Poll stub, copied to WRAM: loops until SFR bit5 (GO) clears.
poll_stub:
    lda $3030
    and #$20
    bne poll_stub        ; assembles bank-0 absolute: fine from WRAM
    rts
poll_stub_end:

; ---------------------------------------------------------------------------
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

.segment "RODATA"
; GSU program (hand-assembled). Registers: R1=x, R2=y, R3=64, R12/R13 loop.
gsu_code:
    .byte $f3, $40, $00              ; IWT R3,#64
    .byte $f2, $00, $00              ; IWT R2,#0
gsu_outer:
    .byte $f1, $00, $00              ; IWT R1,#0
    .byte $fc, $40, $00              ; IWT R12,#64
    .byte $fd, <gsu_inner, >gsu_inner ; IWT R13,#inner
gsu_inner:
    .byte $10                        ; TO R0
    .byte $b1                        ; FROM R1
    .byte $3d, $c2                   ; ALT1; XOR R2  -> R0 = x ^ y
    .byte $b0                        ; FROM R0
    .byte $4e                        ; COLOR
    .byte $4c                        ; PLOT (x++, uses COLR)
    .byte $3c                        ; LOOP (branch R13, delay slot next)
    .byte $01                        ; NOP (delay slot)
    .byte $d2                        ; INC R2
    .byte $b2                        ; FROM R2
    .byte $3f, $63                   ; ALT3; CMP R3
gsu_bne:
    .byte $08, <(gsu_outer - (gsu_bne + 2)) ; BNE outer (delay slot follows)
    .byte $01                        ; NOP (delay slot)
    .byte $00                        ; STOP
    .byte $01                        ; NOP (pipe slot after STOP)

.segment "EXTHEADER"
    .byte $00, $00, $00, $00, $00, $00, $00, $00
    .byte $00, $00, $00, $00, $00
    .byte $06                        ; $FFBD: 64 KB expansion RAM
    .byte $00, $00

.segment "SNESHEADER"
    .byte "SUPERFX GSU TEST     "   ; 21-char title
    .byte $20                        ; LoROM, slow
    .byte $15                        ; chip: SuperFX + RAM
    .byte $06                        ; 64 KB
    .byte $00
    .byte $01                        ; NTSC
    .byte $33
    .byte $00
    .byte $FF, $FF
    .byte $00, $00

.segment "VECTORS"
    .word stub, stub, stub, nmi, stub, stub, stub, stub
    .word stub, stub, stub, stub, reset, stub
