# NUSA GitHub Automation Bot

Official automation identity for new NUSA GitHub App-authored commits:
**`stefendy-nusa[bot]`**.

Production automation must authenticate with the installed GitHub App
(`NUSA_APP_CLIENT_ID` variable plus `NUSA_APP_PRIVATE_KEY` Actions secret).
Changing only `git config user.name` while still using `GITHUB_TOKEN`
does not replace the actual GitHub App authentication.

Historical commits made by `github-actions[bot]` remain part of the
original firmware build provenance and are intentionally preserved.
