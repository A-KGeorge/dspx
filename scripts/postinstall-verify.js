#!/usr/bin/env node

/**
 * Post-install verification script
 * Ensures the native addon loaded successfully
 */

try {
  // Try to load the native addon directly
  const nodeGypBuild = await import("node-gyp-build");
  const { dirname, join } = await import("path");
  const { fileURLToPath } = await import("url");

  const __filename = fileURLToPath(import.meta.url);
  const __dirname = dirname(__filename);
  const projectRoot = join(__dirname, "..");

  const addon = nodeGypBuild.default(projectRoot);

  console.log("Native addon loaded successfully!");
  console.log("   dspx is ready to use.");
  process.exit(0);
} catch (error) {
  console.error("❌ Failed to load native addon:");
  console.error(error.message);
  console.error("\n💡 This usually means:");
  console.error("   1. Compilation failed (check logs above)");
  console.error("   2. No prebuilt binary for your platform");
  console.error("   3. Node.js version mismatch");
  console.error("\nSee: https://github.com/A-KGeorge/dspx#installation");
  process.exit(1);
}
