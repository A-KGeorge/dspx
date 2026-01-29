# Trusted Publishing Setup Guide

This project uses **npm trusted publishing with OIDC** for secure, token-free package releases.

## ✅ What's Already Configured

- ✅ CI workflow has `id-token: write` permission
- ✅ Node.js setup includes `registry-url: 'https://registry.npmjs.org'`
- ✅ NPM_TOKEN removed (not needed with OIDC!)
- ✅ `package.json` requires npm >= 11.5.1

## 🚀 One-Time Setup Steps

### Step 1: Configure Trusted Publisher on npm

1. **Go to your package on npm:** https://www.npmjs.com/package/dspx/access

   - If the package doesn't exist yet, you'll configure this after the first manual publish

2. **Add GitHub Actions as Trusted Publisher:**
   - Click "Publishing access" tab
   - Find "Trusted Publisher" section
   - Click **"GitHub Actions"** button
   - Fill in the form:
     ```
     Organization or user: A-KGeorge
     Repository: dspx
     Workflow filename: ci.yml
     Environment name: (leave empty unless using GitHub environments)
     ```
   - Click **"Add trusted publisher"**

### Step 2: First Manual Publish (One-time)

Since the package `dspx` doesn't exist on npm yet, do the first publish manually:

```bash
# Login to npm
npm login

# Make sure you're on the main branch
git checkout main
git pull

# Build the package
npm run build

# Publish (this creates the package on npm)
npm publish --access public
```

After this first publish, all future releases will use trusted publishing automatically!

### Step 3: (Optional) Restrict Token Access

For maximum security, after trusted publishing is working:

1. Go to package settings: https://www.npmjs.com/package/dspx/access
2. Under "Publishing access"
3. Select **"Require two-factor authentication and disallow tokens"**
4. Click "Update Package Settings"

This prevents traditional token-based publishing while keeping OIDC working.

## 🔄 How Releases Work

Once set up, releases are **fully automated**:

1. **Create a changeset:**

   ```bash
   npm run changeset
   # Follow prompts to describe your changes
   git add .
   git commit -m "chore: add changeset"
   git push
   ```

2. **Automated PR Creation:**

   - Changesets bot creates a "Version Packages" PR
   - PR updates version numbers and CHANGELOG

3. **Merge to Release:**
   - When you merge the PR to `main`
   - CI automatically publishes to npm using OIDC
   - **No tokens needed!** 🎉

## 🔒 Security Benefits

- ✅ No long-lived npm tokens in GitHub secrets
- ✅ Short-lived credentials (valid only during publish)
- ✅ Cannot be extracted or reused
- ✅ Automatic provenance attestations
- ✅ Cryptographic proof of package origin

## 🐛 Troubleshooting

### "Unable to authenticate" error

1. Verify workflow filename is exactly `ci.yml` in trusted publisher config
2. Ensure you're using GitHub-hosted runners (not self-hosted)
3. Check that `id-token: write` permission is present
4. Verify the repository and organization names match exactly (case-sensitive)

### Private dependencies failing to install

Trusted publishing only works for **publishing**. For installing private dependencies:

```yaml
- run: npm ci
  env:
    NODE_AUTH_TOKEN: ${{ secrets.NPM_READ_TOKEN }} # Read-only token
```

### Package doesn't appear to be published

1. Check the Actions tab for errors: https://github.com/A-KGeorge/dspx/actions
2. Verify the workflow ran on the `main` branch
3. Ensure the "Version Packages" PR was merged

## 📚 References

- [npm Trusted Publishing Docs](https://docs.npmjs.com/generating-provenance-statements)
- [GitHub OIDC Docs](https://docs.github.com/en/actions/deployment/security-hardening-your-deployments/about-security-hardening-with-openid-connect)
- [Changesets Documentation](https://github.com/changesets/changesets)
