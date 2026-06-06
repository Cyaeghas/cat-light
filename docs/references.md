# References

Useful implementation facts collected for this project:

- Codex CLI file auth is stored under `~/.codex/auth.json` and contains `tokens.access_token`, `tokens.refresh_token`, and usually `tokens.account_id`.
- Codex usage is fetched from `https://chatgpt.com/backend-api/wham/usage` with a bearer token. Some accounts also need `ChatGPT-Account-Id`.
- Codex OAuth refresh uses `POST https://auth.openai.com/oauth/token` with client id `app_EMoamEEZ73f0CkXaXp7hrann`.
- Claude Code credentials are stored at `~/.claude/.credentials.json` on Windows/Linux unless `CLAUDE_CONFIG_DIR` is set.
- Claude usage is fetched from `https://api.anthropic.com/api/oauth/usage` with `anthropic-beta: oauth-2025-04-20`.
- These endpoints are not stable public APIs, so parsers should be permissive and errors should be visible.
