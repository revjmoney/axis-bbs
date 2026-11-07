; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 Rev. J. Money
;
; AXIS FOUR. A two-seat Connect Four door for the AXIS 8086 runtime.
; FOSSIL on INT 14h, DOOR.SYS for the caller's name, TABLE.DAT shared
; by every node (lock the whole 128-byte record with INT 21h AH=5Ch).
; Args are the node number (%N). Node 1 reads DOOR.SYS. Any other node
; reads NODE<n>\DOOR.SYS, which is where a multinode drop file is written.
;
; Seat 0 is red and moves first. Seat 1 is yellow. Q stands up.
; R after a finished game starts another with the same two seats.

SEQ    equ 4
PHASE  equ 6
TURN   equ 7
WINNER equ 8
MOVES  equ 9
NAME0  equ 12
NAME1  equ 36
NODE0  equ 60
NODE1  equ 61
CELLS  equ 64

psp     dw 0
th      dw 0
dlen    dw 0
node    db 0
me      db 2
pick    db 0
full    db 0
myname  db "Player", 0
        rb 17
tname   db "TABLE.DAT", 0
dname   db "DOOR.SYS", 0
npath   db "NODE0", 5Ch, "DOOR.SYS", 0
tbl     rb 128
dbuf    rb 1024
outbuf  rb 2000

c_off   db 27, "[0m", 0
c_w     db 27, "[1;37m", 0
c_r     db 27, "[1;31m", 0
c_y     db 27, "[1;33m", 0
c_d     db 27, "[0;36m", 0
cls     db 27, "[2J", 27, "[H", 0
s_title db "AXIS FOUR", 0
s_sub   db "Two nodes, one table. Drop four in a row.", 0
s_cols  db "  1 2 3 4 5 6 7", 0
s_red   db "Red ", 0
s_yel   db "Yellow ", 0
s_vs    db "   ", 0
s_node  db "  node ", 0
s_empty db "(empty seat)", 0
s_wait  db "Waiting for the other node. Q stands up.", 0
s_your  db "Your drop. Press 1-7. Q stands up.", 0
s_their db "Waiting for ", 0
s_dot   db ".", 0
s_win   db "You win. R plays again. Q stands up.", 0
s_lose  db "You lose. R plays again. Q stands up.", 0
s_draw  db "Draw. R plays again. Q stands up.", 0
s_watch db "Watching this table. Q quits.", 0
s_full  db "That column is full.", 0

start:
    mov ax, ds
    mov bx, cs
    mov ds, bx
    mov es, bx
    mov [psp], ax
    call read_tail
    call read_drop
    mov ah, 4
    xor dx, dx
    int 14h
    cmp ax, 1954h
    jne no_fossil
    mov ah, 3Dh
    mov al, 2
    mov dx, tname
    int 21h
    jnc table_open
    mov ah, 3Ch
    xor cx, cx
    mov dx, tname
    int 21h
    jc no_table
table_open:
    mov [th], ax

frame:
    call refresh
    cmp byte [me], 2
    jae soft_key
    cmp byte [tbl+PHASE], 2
    jne soft_key
    mov al, [tbl+TURN]
    cmp al, [me]
    jne soft_key
    mov ah, 2
    xor dx, dx
    int 14h
    cmp ah, 80h
    je quit
    call norm
    cmp al, 'Q'
    je leave
    cmp al, '1'
    jb frame
    cmp al, '7'
    ja frame
    sub al, '1'
    mov [pick], al
    call lock
    jc frame
    call read_tbl
    mov al, [tbl+TURN]
    cmp al, [me]
    jne drop_skip
    cmp byte [tbl+PHASE], 2
    jne drop_skip
    mov al, [pick]
    call drop
    jc dropped
    mov byte [full], 1
dropped:
drop_skip:
    call unlock
    jmp frame

soft_key:
    mov ah, 0Ch
    xor dx, dx
    int 14h
    cmp ax, 0FFFFh
    je idle
    mov ah, 2
    xor dx, dx
    int 14h
    call norm
    cmp al, 'Q'
    jne not_q
    cmp byte [me], 2
    jae quit
    jmp leave
not_q:
    cmp al, 'R'
    jne frame
    cmp byte [me], 2
    jae frame
    cmp byte [tbl+PHASE], 3
    jne frame
    call lock
    jc frame
    call read_tbl
    cmp byte [tbl+PHASE], 3
    jne rematch_no
    call clear_board
    mov byte [tbl+PHASE], 2
    mov byte [tbl+TURN], 0
    inc word [tbl+SEQ]
    call write_tbl
rematch_no:
    call unlock
    jmp frame

idle:
    mov ax, 1000h
    int 15h
    jmp frame

leave:
    call lock
    jc quit
    call read_tbl
    cmp byte [me], 0
    jne leave_1
    mov byte [tbl+NAME0], 0
    jmp leave_cleared
leave_1:
    cmp byte [me], 1
    jne leave_cleared
    mov byte [tbl+NAME1], 0
leave_cleared:
    call clear_board
    cmp byte [tbl+NAME0], 0
    jne leave_one
    cmp byte [tbl+NAME1], 0
    jne leave_one
    mov byte [tbl+PHASE], 0
    jmp leave_save
leave_one:
    mov byte [tbl+PHASE], 1
leave_save:
    mov byte [tbl+TURN], 0
    mov byte [tbl+WINNER], 0
    inc word [tbl+SEQ]
    call write_tbl
    call unlock
    jmp quit

no_fossil:
    mov ax, 4C02h
    int 21h
no_table:
    mov ax, 4C01h
    int 21h
quit:
    mov bx, [th]
    or bx, bx
    jz quit_now
    mov ah, 3Eh
    int 21h
quit_now:
    mov ax, 4C00h
    int 21h

refresh:
    call lock
    jnc refresh_got
    mov ax, 1000h
    int 15h
    jmp refresh
refresh_got:
    call read_tbl
    call magic
    call claim
    call unlock
    call draw
    mov ah, 8
    xor dx, dx
    int 14h
    ret

lock:
    mov bx, [th]
    xor cx, cx
    xor dx, dx
    xor si, si
    mov di, 128
    mov ax, 5C00h
    int 21h
    ret

unlock:
    mov bx, [th]
    xor cx, cx
    xor dx, dx
    xor si, si
    mov di, 128
    mov ax, 5C01h
    int 21h
    ret

read_tbl:
    mov bx, [th]
    mov ax, 4200h
    xor cx, cx
    xor dx, dx
    int 21h
    mov ah, 3Fh
    mov cx, 128
    mov dx, tbl
    int 21h
    cmp ax, 128
    jae read_full
    mov cx, 128
    sub cx, ax
    mov di, tbl
    add di, ax
    xor al, al
    cld
    rep stosb
read_full:
    ret

write_tbl:
    mov bx, [th]
    mov ax, 4200h
    xor cx, cx
    xor dx, dx
    int 21h
    mov ah, 40h
    mov cx, 128
    mov dx, tbl
    int 21h
    ret

magic:
    cmp byte [tbl], 'A'
    jne magic_init
    cmp byte [tbl+1], 'X'
    jne magic_init
    cmp byte [tbl+2], '4'
    jne magic_init
    cmp byte [tbl+3], 1
    je magic_ok
magic_init:
    mov di, tbl
    mov cx, 128
    xor al, al
    cld
    rep stosb
    mov byte [tbl], 'A'
    mov byte [tbl+1], 'X'
    mov byte [tbl+2], '4'
    mov byte [tbl+3], 1
    call write_tbl
magic_ok:
    ret

claim:
    mov byte [me], 2
    mov si, tbl+NAME0
    call name_is_me
    jnz claim_try1
    mov byte [me], 0
    ret
claim_try1:
    mov si, tbl+NAME1
    call name_is_me
    jnz claim_take
    mov byte [me], 1
    ret
claim_take:
    cmp byte [tbl+NAME0], 0
    jne claim_seat1
    mov si, myname
    mov di, tbl+NAME0
    call copynz
    mov al, [node]
    mov [tbl+NODE0], al
    mov byte [me], 0
    jmp claim_phase
claim_seat1:
    cmp byte [tbl+NAME1], 0
    jne claim_full
    mov si, myname
    mov di, tbl+NAME1
    call copynz
    mov al, [node]
    mov [tbl+NODE1], al
    mov byte [me], 1
claim_phase:
    cmp byte [tbl+NAME0], 0
    je claim_only
    cmp byte [tbl+NAME1], 0
    je claim_only
    cmp byte [tbl+PHASE], 2
    je claim_full
    call clear_board
    mov byte [tbl+PHASE], 2
    mov byte [tbl+TURN], 0
    jmp claim_bump
claim_only:
    cmp byte [tbl+PHASE], 1
    je claim_bump
    call clear_board
    mov byte [tbl+PHASE], 1
    mov byte [tbl+TURN], 0
claim_bump:
    inc word [tbl+SEQ]
    call write_tbl
claim_full:
    ret

clear_board:
    push di
    cld
    mov di, tbl+CELLS
    mov cx, 42
    xor al, al
    rep stosb
    mov byte [tbl+MOVES], 0
    mov byte [tbl+WINNER], 0
    pop di
    ret

drop:
    mov bl, al
    xor bh, bh
drop_find:
    call getcell
    or al, al
    jz drop_place
    inc bh
    cmp bh, 6
    jb drop_find
    clc
    ret
drop_place:
    mov al, [me]
    inc al
    push ax
    call setcell
    pop ax
    mov dl, al
    inc byte [tbl+MOVES]
    call checkwin
    jc drop_won
    cmp byte [tbl+MOVES], 42
    jae drop_draw
    mov al, [me]
    xor al, 1
    mov [tbl+TURN], al
    jmp drop_save
drop_won:
    mov al, [me]
    inc al
    mov [tbl+WINNER], al
    mov byte [tbl+PHASE], 3
    jmp drop_save
drop_draw:
    mov byte [tbl+WINNER], 3
    mov byte [tbl+PHASE], 3
drop_save:
    inc word [tbl+SEQ]
    call write_tbl
    stc
    ret

getcell:
    push bx
    mov al, bh
    mov ah, 7
    mul ah
    xor ah, ah
    add al, bl
    adc ah, 0
    mov si, ax
    mov al, [tbl+CELLS+si]
    pop bx
    ret

setcell:
    push bx
    push ax
    mov al, bh
    mov ah, 7
    mul ah
    xor ah, ah
    add al, bl
    adc ah, 0
    mov si, ax
    pop bx
    mov [tbl+CELLS+si], bl
    pop bx
    ret

checkwin:
    push bx
    push cx
    push dx
    mov cl, 1
    mov ch, 0
    call countray
    cmp al, 4
    jae check_yes
    mov cl, 0
    mov ch, 1
    call countray
    cmp al, 4
    jae check_yes
    mov cl, 1
    mov ch, 1
    call countray
    cmp al, 4
    jae check_yes
    mov cl, 1
    mov ch, 0FFh
    call countray
    cmp al, 4
    jae check_yes
    clc
    jmp check_out
check_yes:
    stc
check_out:
    pop dx
    pop cx
    pop bx
    ret

countray:
    call countone
    push ax
    neg cl
    neg ch
    call countone
    pop bx
    add al, bl
    inc al
    ret

countone:
    push bx
    xor al, al
count_step:
    add bl, cl
    add bh, ch
    cmp bl, 0
    jl count_out
    cmp bl, 7
    jge count_out
    cmp bh, 0
    jl count_out
    cmp bh, 6
    jge count_out
    push ax
    push cx
    mov al, bh
    mov ah, 7
    mul ah
    xor ah, ah
    add al, bl
    adc ah, 0
    mov si, ax
    mov dh, [tbl+CELLS+si]
    pop cx
    pop ax
    cmp dh, dl
    jne count_out
    inc al
    jmp count_step
count_out:
    pop bx
    ret

name_is_me:
    cmp byte [si], 0
    jz name_no
    push si
    mov di, myname
name_loop:
    mov al, [si]
    mov ah, [di]
    call fold
    xchg al, ah
    call fold
    cmp al, ah
    jne name_popno
    or al, al
    jz name_popyes
    inc si
    inc di
    jmp name_loop
name_popyes:
    pop si
    cmp ax, ax
    ret
name_popno:
    pop si
name_no:
    or ax, 1
    ret

fold:
    cmp al, 'a'
    jb fold_keep
    cmp al, 'z'
    ja fold_keep
    sub al, 32
fold_keep:
    ret

copynz:
    mov cx, 23
copy_loop:
    mov al, [si]
    or al, al
    jz copy_end
    mov [di], al
    inc si
    inc di
    loop copy_loop
copy_end:
    mov byte [di], 0
    ret

norm:
    cmp al, 'a'
    jb norm_keep
    cmp al, 'z'
    ja norm_keep
    sub al, 32
norm_keep:
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
    jne drop_have_node
    mov byte [node], 1
drop_have_node:
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
    mov si, s_cols
    call puts
    call crlf
    mov cx, 6
    mov bh, 5
draw_row:
    push cx
    mov al, ' '
    call putc
    mov al, ' '
    call putc
    xor bl, bl
draw_col:
    call getcell
    cmp al, 1
    je draw_red
    cmp al, 2
    je draw_yel
    mov si, c_d
    call puts
    mov al, '.'
    jmp draw_ch
draw_red:
    mov si, c_r
    call puts
    mov al, 'O'
    jmp draw_ch
draw_yel:
    mov si, c_y
    call puts
    mov al, 'O'
draw_ch:
    call putc
    mov al, ' '
    call putc
    inc bl
    cmp bl, 7
    jb draw_col
    call crlf
    pop cx
    dec bh
    loop draw_row
    call crlf
    mov si, c_r
    call puts
    mov si, s_red
    call puts
    mov si, tbl+NAME0
    cmp byte [si], 0
    jne draw_n0
    mov si, s_empty
draw_n0:
    call puts
    mov si, c_w
    call puts
    cmp byte [tbl+NAME0], 0
    je draw_n0b
    mov si, s_node
    call puts
    mov al, [tbl+NODE0]
    call putdec
draw_n0b:
    mov si, s_vs
    call puts
    mov si, c_y
    call puts
    mov si, s_yel
    call puts
    mov si, tbl+NAME1
    cmp byte [si], 0
    jne draw_n1
    mov si, s_empty
draw_n1:
    call puts
    mov si, c_w
    call puts
    cmp byte [tbl+NAME1], 0
    je draw_n1b
    mov si, s_node
    call puts
    mov al, [tbl+NODE1]
    call putdec
draw_n1b:
    call crlf
    cmp byte [full], 0
    je draw_status
    mov si, c_r
    call puts
    mov si, s_full
    call puts
    call crlf
    mov byte [full], 0
    mov si, c_w
    call puts
draw_status:
    cmp byte [me], 2
    jb draw_seated
    mov si, s_watch
    call puts
    jmp draw_end
draw_seated:
    cmp byte [tbl+PHASE], 3
    jne draw_play
    mov al, [me]
    inc al
    cmp al, [tbl+WINNER]
    jne draw_notwin
    mov si, s_win
    jmp draw_msg
draw_notwin:
    cmp byte [tbl+WINNER], 3
    jne draw_loss
    mov si, s_draw
    jmp draw_msg
draw_loss:
    mov si, s_lose
    jmp draw_msg
draw_play:
    cmp byte [tbl+PHASE], 2
    jne draw_waiting
    mov al, [tbl+TURN]
    cmp al, [me]
    jne draw_opp
    mov si, s_your
    jmp draw_msg
draw_opp:
    mov si, s_their
    call puts
    cmp byte [me], 0
    jne draw_opp0
    mov si, tbl+NAME1
    jmp draw_oppname
draw_opp0:
    mov si, tbl+NAME0
draw_oppname:
    cmp byte [si], 0
    jne draw_oppput
    mov si, s_empty
draw_oppput:
    call puts
    mov si, s_dot
    jmp draw_msg
draw_waiting:
    mov si, s_wait
draw_msg:
    call puts
draw_end:
    call crlf
    mov al, 0
    mov [di], al
    push di
    mov cx, di
    sub cx, outbuf
    mov di, outbuf
    push ds
    pop es
    mov ah, 19h
    xor dx, dx
    int 14h
    pop di
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

putdec:
    xor ah, ah
    mov bl, 10
    div bl
    cmp al, 0
    je putdec_one
    add al, '0'
    push ax
    call putc
    pop ax
putdec_one:
    mov al, ah
    add al, '0'
    call putc
    ret
