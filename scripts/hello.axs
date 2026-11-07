; A program loaded from the card. More .axs files in this folder show up on the Scripts door.
cls
say |14Scripts|07
say
say Hello, @HANDLE.
say This program was loaded from the SD card.
say Board @BBS, node @NODE, @TIME
say
pause
ask NAME Type your name, then Enter
if NAME = @HANDLE
say You typed your own handle.
else
if NAME =
say You left it blank.
else
say Good to meet you, @NAME.
end
end
pause
exit
