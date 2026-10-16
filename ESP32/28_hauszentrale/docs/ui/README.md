# UI drafts

Portrait panel screens, 480×800. `mockup.html` is the source. The PNGs next to it are the output of `render.sh`.

```bash
./render.sh            # every screen
./render.sh home power # only the named screens
```

Google Chrome is required. Set `CHROME` if the binary is not in the usual place. The page loads Montserrat from jsDelivr when it opens, so the first run needs a network. Those font files are not stored in git. If the fetch fails, the page uses Avenir Next or Helvetica Neue instead.
