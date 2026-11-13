# The web installer (FM-1, SMK-37)

One static page that installs the firmware over Web MIDI SysEx (Chrome or Edge on a computer), as
`third_party/felucca-bsp/tools/fm1_install.py` does: the running firmware checks the package and starts the update loader,
the loader writes it, the device restarts and its identity is checked. The firmware's own boot check refuses a non-stock
bootloader (the page then says the device stopped answering and nothing was written).

```bash
bash platforms/fm1hw/build.sh && bash platforms/smk37/build.sh   # the packages
bash platforms/web/installer/build.sh                            # -> build/installer/
node tests/installer_test.mjs build/installer/*.fwsc              # the updater against a simulated device
```

`build/installer/`: `index.html`, `installer.js`, `icon-192.png`, the packages (`fune-fm1.fwsc`, `fune-smk37-pro.fwsc`,
`fune-smk37-elite.fwsc`), `packages.json` (identity, size, SHA-256 of each: the page checks them after the download, since
a missing file on the site comes back as its home page with 200), `LICENSE.txt`, `THIRD_PARTY_NOTICES.txt`.
The shown name is one constant (`NAME` in `index.html`).

## Where it goes (hugelton.com, 2026-11-13)

As the site's other pages: `hugelton/hugelton.com`, `public/<slug>/` = `https://hugelton.com/<slug>/`. Deploys only by a
push to main (GitHub Actions to Cloudflare Pages). While the name is private: a neutral slug, the path in `public/_headers`
with `X-Robots-Tag: noindex, nofollow` (the page also has the robots meta), linked from nowhere. Nothing goes there until
the owner says so.

Not tried on a real device from the browser yet (the app's built-in browser has no Web MIDI): try it in Chrome with the
FM-1 connected, after `save` on its console.
