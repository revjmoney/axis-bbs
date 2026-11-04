"""The tray icon: Open panel, Quit. Runs on its own thread next to the asyncio loop."""
import threading
import webbrowser

from PIL import Image, ImageDraw
import pystray


def icon_image():
    img = Image.new('RGB', (64, 64), (0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle((6, 6, 58, 58), outline=(170, 85, 0), width=4)
    d.text((18, 22), 'AX', fill=(255, 255, 85))
    return img


def start(url, on_quit):
    def quit_(icon, _item):
        icon.stop()
        on_quit()

    icon = pystray.Icon('axispanel', icon_image(), 'AXIS sysop panel', menu=pystray.Menu(
        pystray.MenuItem('Open panel', lambda i, _: webbrowser.open(url), default=True),
        pystray.MenuItem('Quit', quit_)))
    threading.Thread(target=icon.run, daemon=True).start()
    return icon
