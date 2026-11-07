; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Rev. J. Money
;
; LORD Fast. A small door that reads the installed LORD 4.07 PLAYER.DAT
; and LENEMY.DAT. Real LORD.EXE stays on the menu. This program does not
; create a warrior and does not copy the game's prose into the program.
; Monster names come off the disk at runtime.
;
; PLAYER.DAT is a packed Turbo Pascal player_info, 236 bytes, no header.
; The caller is matched to the in-game name or the BBS name (DOOR.SYS
; alias). Forest and the inn rewrite only that one record.

P_REC   equ 236
O_REAL  equ 21
O_HIT   equ 72
O_HITMAX equ 78
O_FIGHTS equ 105
O_HUMAN equ 107
O_GOLD  equ 109
O_BANK  equ 113
O_DEF   equ 117
O_STR   equ 119
O_CHARM equ 121
O_LEVEL equ 127
O_ARM   equ 131
O_DEAD  equ 154
O_INN   equ 155
O_EXP   equ 158
O_CLASS equ 179

M_REC   equ 239
M_STR   equ 61
M_GOLD  equ 65
M_XP    equ 130
M_HP    equ 134

psp     dw 0
ph      dw 0FFFFh
eh      dw 0FFFFh
fsize   dw 0
esize   dw 0
pcount  dw 0
slot    dw 0
node    db 0
have    db 0
showmon db 0
rounds  db 0
note    dw 0
mhp     dw 0
mstr    dw 0
php     dw 0
price   dw 0
myname  db "Player", 0
        rb 17
tmp     rb 24
pname   db "PLAYER.DAT", 0
ename   db "LENEMY.DAT", 0
dname   db "DOOR.SYS", 0
npath   db "NODE0", 5Ch, "DOOR.SYS", 0
ply     rb 236
mon     rb 239
dbuf    rb 1024
dlen    dw 0
outbuf  rb 1200

c_off   db 27, "[0m", 0
c_w     db 27, "[1;37m", 0
c_d     db 27, "[0;36m", 0
c_g     db 27, "[1;33m", 0
cls     db 27, "[2J", 27, "[H", 0
s_title db "LORD Fast", 0
s_sub   db "Your warrior, without the title picture.", 0
s_level db "   level ", 0
s_hp    db "HP ", 0
s_slash db "/", 0
s_forest db "    forest ", 0
s_players db "    players ", 0
s_gold  db "gold ", 0
s_bank  db "   bank ", 0
s_xp    db "   xp ", 0
s_str   db "str ", 0
s_def   db "   def ", 0
s_charm db "   charm ", 0
s_gap   db "   ", 0
s_menu  db "(F) Forest   (I) Inn   (Q) Quit", 0
s_key   db "Press a key.", 0
s_none  db "No warrior for this name.", 0
s_nofile db "No warrior file here.", 0
s_busy  db "The warrior file is busy.", 0
s_ready db "Choose.", 0
s_nofight db "No forest fights left today.", 0
s_nomon db "No monsters are on this disk.", 0
s_dead  db "This warrior is dead. Real LORD has to bring them back.", 0
s_sleep db "This warrior is at the inn.", 0
s_win   db "You win.", 0
s_flee  db "You get away.", 0
s_rest  db "You rest at the inn.", 0
s_rested db "You are already rested.", 0
s_poor  db "Not enough gold for the inn.", 0
s_faced db "Faced ", 0
s_changed db "That warrior record changed.", 0
s_dk    db "Death Knight", 0
s_my    db "Mystic", 0
s_th    db "Thief", 0
s_war   db "Warrior", 0

start:
    cld
    mov ax, ds
    mov bx, cs
    mov ds, bx
    mov es, bx
    mov [psp], ax
    mov si, s_ready
    mov [note], si
    call read_tail
    call read_drop
    mov ah, 4
    xor dx, dx
    int 14h
    cmp ax, 1954h
    jne no_fossil
    call open_files
    jc no_file
    call find
    jc town
    mov byte [have], 1

town:
    call draw
    mov ah, 8
    xor dx, dx
    int 14h
    call key
    jc quit
    cmp al, 'Q'
    je quit
    cmp byte [have], 0
    je quit
    cmp al, 'F'
    jne town_inn
    call forest
    jmp town
town_inn:
    cmp al, 'I'
    jne town
    call inn
    jmp town

no_fossil:
    mov ax, 4C02h
    int 21h

no_file:
    mov si, s_nofile
    mov [note], si
    call draw
    call key
    jmp quit

quit:
    mov bx, [ph]
    cmp bx, 0FFFFh
    je quit_enemy
    mov ah, 3Eh
    int 21h
quit_enemy:
    mov bx, [eh]
    cmp bx, 0FFFFh
    je quit_now
    mov ah, 3Eh
    int 21h
quit_now:
    mov ax, 4C00h
    int 21h

open_files:
    mov ah, 3Dh
    mov al, 2
    mov dx, pname
    int 21h
    jc open_fail
    mov [ph], ax
    mov bx, ax
    mov ax, 4202h
    xor cx, cx
    xor dx, dx
    int 21h
    mov [fsize], ax
    mov ah, 3Dh
    xor al, al
    mov dx, ename
    int 21h
    jc open_ok
    mov [eh], ax
    mov bx, ax
    mov ax, 4202h
    xor cx, cx
    xor dx, dx
    int 21h
    mov [esize], ax
open_ok:
    clc
    ret
open_fail:
    stc
    ret

find:
    cmp word [fsize], 0
    je find_empty
    call lock_try
    jc find_busy
    mov ax, [fsize]
    xor dx, dx
    mov bx, P_REC
    div bx
    mov [pcount], ax
    xor ax, ax
    mov [slot], ax
find_scan:
    mov ax, [slot]
    cmp ax, [pcount]
    jae find_missing
    call read_ply
    mov si, ply
    call match
    jnc find_hit
    mov si, ply+O_REAL
    call match
    jnc find_hit
    inc word [slot]
    jmp find_scan
find_hit:
    call unlock
    clc
    ret
find_missing:
    call unlock
find_empty:
    mov si, s_none
    mov [note], si
    stc
    ret
find_busy:
    mov si, s_busy
    mov [note], si
    stc
    ret

lock_try:
    mov cx, 5
lock_attempt:
    push cx
    call lock
    jnc lock_held
    mov ax, 1000h
    int 15h
    pop cx
    loop lock_attempt
    stc
    ret
lock_held:
    pop cx
    clc
    ret

lock:
    mov bx, [ph]
    xor cx, cx
    xor dx, dx
    xor si, si
    mov di, [fsize]
    mov ax, 5C00h
    int 21h
    ret

unlock:
    mov bx, [ph]
    xor cx, cx
    xor dx, dx
    xor si, si
    mov di, [fsize]
    mov ax, 5C01h
    int 21h
    ret

read_ply:
    mov ax, [slot]
    mov bx, P_REC
    mul bx
    mov cx, dx
    mov dx, ax
    mov bx, [ph]
    mov ax, 4200h
    int 21h
    mov ah, 3Fh
    mov cx, P_REC
    mov dx, ply
    mov bx, [ph]
    int 21h
    ret

write_ply:
    mov ax, [slot]
    mov bx, P_REC
    mul bx
    mov cx, dx
    mov dx, ax
    mov bx, [ph]
    mov ax, 4200h
    int 21h
    mov ah, 40h
    mov cx, P_REC
    mov dx, ply
    mov bx, [ph]
    int 21h
    ret

still_us:
    mov si, ply
    call match
    jnc still_ok
    mov si, ply+O_REAL
    call match
still_ok:
    ret

forest:
    cmp byte [ply+O_DEAD], 5
    je forest_dead
    cmp byte [ply+O_INN], 5
    je forest_sleep
    cmp word [ply+O_HIT], 0
    jle forest_dead
    cmp word [ply+O_FIGHTS], 0
    je forest_none
    cmp word [eh], 0FFFFh
    je forest_nomon
    call lock_try
    jc forest_busy
    call read_ply
    call still_us
    jc forest_changed
    cmp word [ply+O_FIGHTS], 0
    je forest_none_locked
    call pick_mon
    jc forest_nomon_locked
    mov si, mon+M_HP
    call monlow
    mov [mhp], ax
    mov si, mon+M_STR
    call monlow
    mov [mstr], ax
    mov ax, [ply+O_HIT]
    mov [php], ax
    mov byte [rounds], 40
fight:
    cmp word [mhp], 0
    je fight_won
    cmp byte [rounds], 0
    je fight_flee
    dec byte [rounds]
    mov ax, [ply+O_STR]
    call halfmin
    mov bx, [mhp]
    cmp ax, bx
    jb fight_cut
    xor bx, bx
    jmp fight_store
fight_cut:
    sub bx, ax
fight_store:
    mov [mhp], bx
    cmp bx, 0
    je fight_won
    mov ax, [mstr]
    call halfmin
    mov bx, [php]
    cmp ax, bx
    jb fight_hurt
    mov word [php], 1
    jmp fight_flee
fight_hurt:
    sub bx, ax
    mov [php], bx
    jmp fight

fight_won:
    mov ax, [php]
    mov [ply+O_HIT], ax
    dec word [ply+O_FIGHTS]
    mov ax, [ply+O_GOLD]
    mov dx, [ply+O_GOLD+2]
    add ax, [mon+M_GOLD]
    adc dx, [mon+M_GOLD+2]
    mov [ply+O_GOLD], ax
    mov [ply+O_GOLD+2], dx
    mov ax, [ply+O_EXP]
    mov dx, [ply+O_EXP+2]
    add ax, [mon+M_XP]
    adc dx, [mon+M_XP+2]
    mov [ply+O_EXP], ax
    mov [ply+O_EXP+2], dx
    call write_ply
    call unlock
    mov byte [showmon], 1
    mov si, s_win
    jmp forest_note

fight_flee:
    mov ax, [php]
    mov [ply+O_HIT], ax
    dec word [ply+O_FIGHTS]
    call write_ply
    call unlock
    mov byte [showmon], 1
    mov si, s_flee
    jmp forest_note

forest_none_locked:
    call unlock
forest_none:
    mov si, s_nofight
    jmp forest_note
forest_nomon_locked:
    call unlock
forest_nomon:
    mov si, s_nomon
    jmp forest_note
forest_busy:
    mov si, s_busy
    jmp forest_note
forest_changed:
    call unlock
    mov si, s_changed
    jmp forest_note
forest_dead:
    mov si, s_dead
    jmp forest_note
forest_sleep:
    mov si, s_sleep
    jmp forest_note
forest_note:
    mov [note], si
    ret

inn:
    cmp byte [ply+O_DEAD], 5
    je forest_dead
    mov ax, [ply+O_HIT]
    cmp ax, [ply+O_HITMAX]
    jl inn_need
    mov si, s_rested
    jmp forest_note
inn_need:
    mov ax, [ply+O_HITMAX]
    sub ax, [ply+O_HIT]
    mov bx, ax
    add ax, ax
    add ax, bx
    mov [price], ax
    call lock_try
    jc forest_busy
    call read_ply
    call still_us
    jc forest_changed
    mov ax, [ply+O_HIT]
    cmp ax, [ply+O_HITMAX]
    jl inn_price
    call unlock
    mov si, s_rested
    jmp forest_note
inn_price:
    mov ax, [ply+O_HITMAX]
    sub ax, [ply+O_HIT]
    mov bx, ax
    add ax, ax
    add ax, bx
    mov [price], ax
    mov dx, [ply+O_GOLD+2]
    cmp dx, 0
    jl inn_poor
    jg inn_pay
    mov ax, [ply+O_GOLD]
    cmp ax, [price]
    jb inn_poor
inn_pay:
    mov ax, [ply+O_GOLD]
    mov dx, [ply+O_GOLD+2]
    sub ax, [price]
    sbb dx, 0
    mov [ply+O_GOLD], ax
    mov [ply+O_GOLD+2], dx
    mov ax, [ply+O_HITMAX]
    mov [ply+O_HIT], ax
    call write_ply
    call unlock
    mov si, s_rest
    jmp forest_note
inn_poor:
    call unlock
    mov si, s_poor
    jmp forest_note

pick_mon:
    mov ax, [esize]
    xor dx, dx
    mov bx, M_REC
    div bx
    or ax, ax
    jz pick_fail
    push ax
    mov ax, [ply+O_LEVEL]
    or ax, ax
    jg pick_lvl
    mov ax, 1
pick_lvl:
    dec ax
    mov bx, 11
    mul bx
    push ax
    xor ah, ah
    int 1Ah
    mov ax, dx
    xor dx, dx
    mov bx, 11
    div bx
    pop bx
    add bx, dx
    pop cx
    cmp bx, cx
    jb pick_have
    mov bx, cx
    dec bx
pick_have:
    mov ax, bx
    mov bx, M_REC
    mul bx
    mov cx, dx
    mov dx, ax
    mov bx, [eh]
    mov ax, 4200h
    int 21h
    mov ah, 3Fh
    mov cx, M_REC
    mov dx, mon
    mov bx, [eh]
    int 21h
    cmp ax, M_REC
    jb pick_fail
    clc
    ret
pick_fail:
    stc
    ret

monlow:
    mov ax, [si]
    mov dx, [si+2]
    or dx, dx
    jns mon_nonneg
    mov ax, 1
    ret
mon_nonneg:
    or dx, dx
    jz mon_small
    mov ax, 30000
    ret
mon_small:
    or ax, ax
    jg mon_ret
    mov ax, 1
mon_ret:
    ret

halfmin:
    or ax, ax
    jle half_one
    xor dx, dx
    mov bx, 2
    div bx
    or ax, ax
    jnz half_ret
half_one:
    mov ax, 1
half_ret:
    ret

match:
    call pas_to
    cmp byte [tmp], 0
    je match_no
    mov si, tmp
    mov di, myname
match_ch:
    mov al, [si]
    mov bl, [di]
    cmp al, 'a'
    jb match_ua
    cmp al, 'z'
    ja match_ua
    sub al, 32
match_ua:
    cmp bl, 'a'
    jb match_ub
    cmp bl, 'z'
    ja match_ub
    sub bl, 32
match_ub:
    cmp al, bl
    jne match_no
    or al, al
    jz match_yes
    inc si
    inc di
    jmp match_ch
match_yes:
    clc
    ret
match_no:
    stc
    ret

pas_to:
    mov cl, [si]
    xor ch, ch
    inc si
    cmp cx, 23
    jbe pas_len
    mov cx, 23
pas_len:
    mov di, tmp
    jcxz pas_term
pas_copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop pas_copy
pas_term:
    mov byte [di], 0
pas_trim:
    cmp di, tmp
    je pas_ret
    dec di
    cmp byte [di], ' '
    jne pas_ret
    mov byte [di], 0
    jmp pas_trim
pas_ret:
    ret

key:
    mov ah, 2
    xor dx, dx
    int 14h
    cmp ah, 80h
    je key_drop
    cmp al, 'a'
    jb key_up
    cmp al, 'z'
    ja key_up
    sub al, 32
key_up:
    clc
    ret
key_drop:
    stc
    ret

draw:
    mov di, outbuf
    mov si, cls
    call puts
    mov si, c_w
    call puts
    mov si, s_title
    call puts
    call crlf
    mov si, c_d
    call puts
    mov si, s_sub
    call puts
    call crlf
    cmp byte [have], 0
    je draw_note
    call crlf
    mov si, c_w
    call puts
    mov si, ply
    call putpas
    mov si, c_d
    call puts
    mov si, s_level
    call puts
    mov ax, [ply+O_LEVEL]
    xor dx, dx
    call putlong
    mov al, ' '
    call putc
    mov al, ' '
    call putc
    call class_name
    call puts
    call crlf
    mov si, c_g
    call puts
    mov si, s_hp
    call puts
    mov ax, [ply+O_HIT]
    xor dx, dx
    call putlong
    mov si, s_slash
    call puts
    mov ax, [ply+O_HITMAX]
    xor dx, dx
    call putlong
    mov si, s_forest
    call puts
    mov ax, [ply+O_FIGHTS]
    xor dx, dx
    call putlong
    mov si, s_players
    call puts
    mov ax, [ply+O_HUMAN]
    xor dx, dx
    call putlong
    call crlf
    mov si, s_gold
    call puts
    mov ax, [ply+O_GOLD]
    mov dx, [ply+O_GOLD+2]
    call putlong
    mov si, s_bank
    call puts
    mov ax, [ply+O_BANK]
    mov dx, [ply+O_BANK+2]
    call putlong
    mov si, s_xp
    call puts
    mov ax, [ply+O_EXP]
    mov dx, [ply+O_EXP+2]
    call putlong
    call crlf
    mov si, c_w
    call puts
    mov si, ply+82
    call putpas
    mov si, s_gap
    call puts
    mov si, ply+O_ARM
    call putpas
    call crlf
    mov si, c_d
    call puts
    mov si, s_str
    call puts
    mov ax, [ply+O_STR]
    xor dx, dx
    call putlong
    mov si, s_def
    call puts
    mov ax, [ply+O_DEF]
    xor dx, dx
    call putlong
    mov si, s_charm
    call puts
    mov ax, [ply+O_CHARM]
    xor dx, dx
    call putlong
    call crlf
    call crlf
    mov si, c_w
    call puts
    mov si, s_menu
    call puts
    call crlf
draw_note:
    cmp byte [showmon], 0
    je draw_msg
    mov si, c_g
    call puts
    mov si, s_faced
    call puts
    mov si, mon
    call putpas
    call crlf
    mov byte [showmon], 0
draw_msg:
    mov si, c_w
    call puts
    mov si, [note]
    call puts
    call crlf
    cmp byte [have], 0
    jne draw_send
    mov si, s_key
    call puts
    call crlf
draw_send:
    mov cx, di
    sub cx, outbuf
    mov di, outbuf
    push ds
    pop es
    mov ah, 19h
    xor dx, dx
    int 14h
    ret

class_name:
    mov al, [ply+O_CLASS]
    cmp al, 1
    jne class_m
    mov si, s_dk
    ret
class_m:
    cmp al, 2
    jne class_t
    mov si, s_my
    ret
class_t:
    cmp al, 3
    jne class_w
    mov si, s_th
    ret
class_w:
    mov si, s_war
    ret

putlong:
    xor si, si
putlong_more:
    call div10
    push cx
    inc si
    or ax, ax
    jnz putlong_more
    or dx, dx
    jnz putlong_more
putlong_digit:
    pop ax
    add al, '0'
    call putc
    dec si
    jnz putlong_digit
    ret

div10:
    push si
    mov bx, 10
    mov si, ax
    mov ax, dx
    xor dx, dx
    div bx
    push ax
    mov ax, si
    div bx
    mov cx, dx
    pop dx
    pop si
    ret

putpas:
    mov cl, [si]
    xor ch, ch
    inc si
    cmp cx, 40
    jbe putpas_len
    mov cx, 40
putpas_len:
    jcxz putpas_done
putpas_loop:
    mov al, [si]
    inc si
    call putc
    loop putpas_loop
putpas_done:
    ret

putc:
    mov [di], al
    inc di
    ret

puts:
    lodsb
    or al, al
    jz puts_done
    mov [di], al
    inc di
    jmp puts
puts_done:
    ret

crlf:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret

read_tail:
    mov es, [psp]
    mov si, 80h
    mov cl, [es:si]
    xor ch, ch
    inc si
    xor bl, bl
    mov bh, 0
tail_loop:
    jcxz tail_done
    mov dl, [es:si]
    inc si
    dec cx
    cmp dl, ' '
    je tail_loop
    cmp dl, '0'
    jb tail_done
    cmp dl, '9'
    ja tail_done
    mov al, bl
    mov ah, 10
    mul ah
    sub dl, '0'
    xor dh, dh
    add ax, dx
    mov bl, al
    mov bh, 1
    jmp tail_loop
tail_done:
    push ds
    pop es
    cmp bh, 0
    je tail_default
    mov [node], bl
    ret
tail_default:
    cmp byte [node], 0
    jne tail_ret
    mov byte [node], 1
tail_ret:
    ret

read_drop:
    cmp byte [node], 0
    jne drop_have
    mov byte [node], 1
drop_have:
    mov dx, dname
    cmp byte [node], 1
    je drop_open
    mov al, [node]
    add al, '0'
    mov [npath+4], al
    mov dx, npath
drop_open:
    mov ah, 3Dh
    xor al, al
    int 21h
    jc drop_noname
    mov bx, ax
    mov ah, 3Fh
    mov cx, 1023
    mov dx, dbuf
    int 21h
    mov [dlen], ax
    mov ah, 3Eh
    int 21h
    mov cx, 4
    call getline
    jc drop_name
    mov cx, ax
    call parse_node
drop_name:
    mov cx, 36
    call getline
    jc drop_noname
    mov cx, ax
    cmp cx, 23
    jbe drop_len
    mov cx, 23
drop_len:
    mov di, myname
    jcxz drop_term
drop_copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop drop_copy
drop_term:
    mov byte [di], 0
drop_trim:
    cmp di, myname
    je drop_noname
    dec di
    cmp byte [di], ' '
    je drop_trim
    inc di
    mov byte [di], 0
    cmp byte [myname], 0
    jne drop_ret
drop_noname:
    mov byte [myname], 'P'
    mov byte [myname+1], 'l'
    mov byte [myname+2], 'a'
    mov byte [myname+3], 'y'
    mov byte [myname+4], 'e'
    mov byte [myname+5], 'r'
    mov byte [myname+6], 0
drop_ret:
    ret

parse_node:
    xor bl, bl
    mov bh, 0
parse_loop:
    jcxz parse_done
    mov al, [si]
    inc si
    dec cx
    cmp al, '0'
    jb parse_loop
    cmp al, '9'
    ja parse_done
    sub al, '0'
    mov dl, al
    mov al, bl
    mov ah, 10
    mul ah
    xor dh, dh
    add al, dl
    mov bl, al
    mov bh, 1
    jmp parse_loop
parse_done:
    cmp bh, 0
    je parse_ret
    cmp bl, 0
    je parse_ret
    mov [node], bl
parse_ret:
    ret

getline:
    push bx
    push dx
    push di
    mov si, dbuf
    mov bx, 1
    mov dx, dbuf
    add dx, [dlen]
get_next:
    cmp si, dx
    jae get_miss
    cmp bx, cx
    je get_got
get_skip:
    cmp si, dx
    jae get_miss
    mov al, [si]
    inc si
    cmp al, 10
    je get_adv
    cmp al, 13
    jne get_skip
    cmp si, dx
    jae get_adv
    cmp byte [si], 10
    jne get_adv
    inc si
get_adv:
    inc bx
    jmp get_next
get_got:
    mov di, si
    xor ax, ax
get_measure:
    cmp si, dx
    jae get_measured
    mov bl, [si]
    cmp bl, 13
    je get_measured
    cmp bl, 10
    je get_measured
    inc si
    inc ax
    jmp get_measure
get_measured:
    mov si, di
    clc
    jmp get_out
get_miss:
    stc
get_out:
    pop di
    pop dx
    pop bx
    ret
