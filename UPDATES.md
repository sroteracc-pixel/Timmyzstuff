# Update button (simple version)

After one-time setup, the loop is:
1. Upload the new files to GitHub (the build starts by itself).
2. Wait for the green check under **Actions**.
3. On the Quest, open Timmyzstuff and press **Update**. Done.

## One-time setup
1. Open `TZ_KEYSTORE_B64.txt` and copy ALL the text inside.
2. GitHub repo -> **Settings** -> **Secrets and variables** -> **Actions** -> **New repository secret**.
3. Name: `TZ_KEYSTORE_B64`. Paste the text as the value. Save.
4. **Actions** -> **Build APK** -> **Run workflow**.
5. Download the APK from the finished run, uninstall the old Timmyzstuff on the Quest, install the new one.
   (This is the ONLY time you install by hand. The signing key is different from before.)

Never upload `TZ_KEYSTORE_B64.txt` to GitHub. Keep it safe: if you lose it, you do step 5 again.

## If Update says something is wrong
- "no build has been published (404)": the secret is missing, or no build has finished yet.
- "signed with a different key": do step 5 again.
- "Already up to date": the build on GitHub is the one you have.

## The update line (top of the app)
When the app opens it looks (only looks) at GitHub and shows one of:
- **Update available - build N**: a newer build exists. Press **Update** to install it. Nothing installs by itself.
- **Up to date**: you have the newest build.
- **Couldn't check for updates**: no internet, GitHub is busy (it limits how often one network may ask), or no build is published yet.
