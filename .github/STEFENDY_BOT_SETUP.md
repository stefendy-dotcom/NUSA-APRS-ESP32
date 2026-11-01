# stefendy[bot] — NUSA GitHub App bootstrap

> Status: staged only. This repository's default branch and production release workflows remain unchanged until app registration, credential setup, and review are completed.

## GitHub account / app identity

- Owner: `stefendy-dotcom`
- Requested GitHub App **name**: `stefendy` (desired actor: `stefendy[bot]`). Exact name subject to GitHub's global availability check.
- Homepage URL: `https://github.com/stefendy-dotcom`
- Description: `NUSA project automation bot for APRS, radio firmware and NUSA RoIP releases.`
- Webhooks: **Active OFF** while used only for installation authentication.
- OAuth user authorization: not required.
- Installation visibility: **Only on this account**.
- Repository permissions: **Contents: Read and write**; Metadata: Read (automatic). Avoid other permissions until a specific task requires them.

## Owner-only setup (one time)

1. Sign in as `stefendy-dotcom`, open `https://github.com/settings/apps/new`, register the GitHub App as `stefendy` with the settings above.
2. Open its settings. Record **Client ID** (not a secret), generate and download a **private key** `.pem` (secret). Do **not** commit, upload, or send the PEM key in chat.
3. Use **Install App** on `stefendy-dotcom`; choose the six NUSA repositories or `All repositories` if that broader access is intended. Selecting six repos will not automatically cover future repos.
4. For **each repository**: Settings → Secrets and variables → Actions → Variables: set `NUSA_APP_CLIENT_ID` to the actual Client ID.
5. For **each repository**: Settings → Secrets and variables → Actions → Secrets: set `NUSA_APP_PRIVATE_KEY` to the exact full PEM key including BEGIN/END lines.
6. Review and merge the prepared branch `chore/stefendy-bot-bootstrap` **after** those values are ready. Then Actions → `Verify stefendy[bot] GitHub App` → Run workflow. The verifier does not commit, push or publish.
7. Confirm log says `Verified GitHub App identity: stefendy[bot]`. Only then migrate existing workflows to use the App token for ALL mutating GitHub API calls, git pushes, and release operations.

## Repositories staged

- `NUSA-APRS-ESP32`
- `NUSA-UVK5-APRS-DIGI-IGATE`
- `NUSA-UV5RH-PRO-V2-APRS`
- `nusa-roip-server`
- `nusa-roip-cloudflare`
- `nusa-roip-firmware`

## Migration pattern (per write-capable workflow)

Before checkout, add an app-token creation step:

```yaml
- uses: actions/create-github-app-token@v3
  id: bot
  with:
    client-id: ${{ vars.NUSA_APP_CLIENT_ID }}
    private-key: ${{ secrets.NUSA_APP_PRIVATE_KEY }}
    permission-contents: write
- uses: actions/checkout@v4
  with:
    token: ${{ steps.bot.outputs.token }}
```

After checkout, configure the Git identity from the real GitHub App bot ID rather than faking a name:

```bash
BOT_SLUG='stefendy'
BOT_LOGIN="${BOT_SLUG}[bot]"
BOT_ID="$(gh api "/users/${BOT_LOGIN}" --jq '.id')"
git config user.name "$BOT_LOGIN"
git config user.email "${BOT_ID}+${BOT_LOGIN}@users.noreply.github.com"
```

Set `GH_TOKEN: ${{ steps.bot.outputs.token }}` for all `gh release` operations and `GITHUB_TOKEN: ${{ steps.bot.outputs.token }}` for scripts that consume this variable. Run `gh api` with `GH_TOKEN` set to the same App token. Do not rely on the automatically supplied `github.token` for writes.

## Existing workflow issues confirmed by audit

Seven workflows under `NUSA-UVK5-APRS-DIGI-IGATE/.github/workflows` currently hard-code `github-actions[bot]`:

- `update-download-counts.yml`
- `publish-igate-rev1a.yml`
- `publish-standalone-next.yml`
- `publish-gps-tracker-rev1d.yml`
- `publish-rev1u.yml`
- `publish-rev1ab.yml`
- `publish-tnc-kiss-rev1a.yml`

**Do not mass-replace the bot name alone**: using `GITHUB_TOKEN` with a fake commit author is not a real GitHub App-authored push/release. Ensure authentication **and** commit author identity both use the installed GitHub App.

**Release safety:** A number of legacy publish workflows delete and recreate releases. Re-running them can reset asset download counts. Do not enable or merge changes that trigger these automatically without reviewing the release behavior. Do not rewrite firmware binaries/checksums.

**Limit:** GitHub Actions remains the CI runner and its UI may still display GitHub Actions as the execution service. The App controls commits, PRs, releases and API writes when used correctly; it does not rename GitHub's built-in `github-actions[bot]` user or the separate ChatGPT GitHub connector.

Official docs: https://docs.github.com/en/apps/creating-github-apps/registering-a-github-app/registering-a-github-app and https://github.com/actions/create-github-app-token
