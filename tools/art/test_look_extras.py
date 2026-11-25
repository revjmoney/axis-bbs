# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""The look packs keep GOODBYE, the list templates and the lite screens, and the installer copies them."""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))

import check_looks  # noqa: E402
import install_cyd  # noqa: E402


class LookPackTest(unittest.TestCase):
    def test_every_look_has_goodbye_lists_and_lite_screens(self):
        self.assertEqual(check_looks.check_repo(ROOT), 0)

    def test_installer_copies_the_axis_folder_next_to_the_numbered_screens(self):
        look, fn, _saylor = check_looks.PACKS[0]
        src = os.path.join(ROOT, 'art', 'packs', fn)
        out = tempfile.mkdtemp()
        try:
            written = dict(install_cyd.map_theme_zip(src, out))
            for name in ('MAIN.ANS', 'GOODBYE.ANS', 'GOODBYE.LITE.ANS', 'MAIN.LITE.ANS',
                         'LOGIN.LITE.ANS', 'FILES.LST', 'MSGS.LST', 'USERS.LST', 'THEME.CFG',
                         'ACCOUNT.ANS', 'ACCOUNT.LITE.ANS', 'ACCOUNT.LST',
                         'WHO.ANS', 'WHO.LITE.ANS', 'WHO.LST',
                         'DOORLB.ANS', 'DOORLB.LITE.ANS', 'DOORLB.LST',
                         'SYSOP.ANS', 'SYSOP.LITE.ANS', 'SYSOP.LST',
                         'MENUS.ANS', 'MENUS.LITE.ANS'):
                self.assertIn(name, written, name)
                self.assertTrue(os.path.isfile(os.path.join(out, name)))
            self.assertLessEqual(written['GOODBYE.LITE.ANS'], 2048)
            self.assertLessEqual(written['MAIN.LITE.ANS'], 2048)
            for name in ('ACCOUNT.LITE.ANS', 'WHO.LITE.ANS', 'DOORLB.LITE.ANS', 'SYSOP.LITE.ANS', 'MENUS.LITE.ANS'):
                self.assertLessEqual(written[name], 700, name)
            with open(os.path.join(out, 'THEME.CFG'), encoding='ascii') as fh:
                text = fh.read()
            self.assertIn('dialect=axis', text)
            self.assertEqual(look, 'crt')
        finally:
            for name in os.listdir(out):
                os.remove(os.path.join(out, name))
            os.rmdir(out)

    def test_dialect_packs_ship_the_screen_set(self):
        import make_dialect_packs
        self.assertEqual(make_dialect_packs.verify(ROOT), 0)
        for look in (make_dialect_packs.VISION, make_dialect_packs.OUTPOST):
            src = os.path.join(ROOT, 'art', 'packs', look.zipname)
            out = tempfile.mkdtemp()
            try:
                written = dict(install_cyd.map_theme_zip(src, out))
                for name in ('MAIN.ANS', 'LOGIN.ANS', 'GOODBYE.ANS', 'BULLETIN.ANS', 'FILES.LST',
                             'MSGS.LST', 'USERS.LST', 'THEME.CFG', 'MAIN.LITE.ANS', 'GOODBYE.LITE.ANS'):
                    self.assertIn(name, written, name)
                    self.assertTrue(os.path.isfile(os.path.join(out, name)))
                self.assertLessEqual(written['MAIN.LITE.ANS'], 700)
                self.assertLessEqual(written['GOODBYE.LITE.ANS'], 700)
                with open(os.path.join(out, 'THEME.CFG'), encoding='ascii') as fh:
                    text = fh.read()
                want = 'dialect=vision3' if look.id == 'vision' else 'dialect=renegade'
                self.assertIn(want, text)
            finally:
                for name in os.listdir(out):
                    os.remove(os.path.join(out, name))
                os.rmdir(out)


if __name__ == '__main__':
    unittest.main()
