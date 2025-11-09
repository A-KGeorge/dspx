#!/usr/bin/env node

/**
 * Test installation behavior
 * Usage: node scripts/test-install.js
 */

import { arch, platform } from "os";
import { existsSync, rmSync } from "fs";
import { execSync } from "child_process";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const projectRoot = join(__dirname, "..");

console.log("\nTesting dspx installation behavior\n");
console.log(`Platform: ${platform()}`);
console.log(`Architecture: ${arch()}`);

const isArm = arch().includes("arm") || arch().includes("aarch");
const buildDir = join(projectRoot, "build");
const prebuildsDir = join(projectRoot, "prebuilds");

console.log(
  `\nBuild directory: ${existsSync(buildDir) ? " exists" : " missing"}`
);
console.log(
  `Prebuilds directory: ${existsSync(prebuildsDir) ? " exists" : " missing"}`
);

console.log("\n Test Options:");
console.log("1. Test with prebuilds (simulate x64 user)");
console.log("2. Test without prebuilds (force compilation)");
console.log("3. Test current state");
console.log("4. Clean all build artifacts");

// For simplicity, run test 3 (current state)
console.log("\n Running test: Current state\n");

try {
  // Try to load the module
  const startTime = Date.now();

  console.log("Attempting to load native addon...");

  execSync("node scripts/postinstall-verify.js", {
    cwd: projectRoot,
    stdio: "inherit",
  });

  const elapsed = Date.now() - startTime;
  console.log(`\n Success! Loaded in ${elapsed}ms`);

  if (isArm) {
    console.log("\n ARM detected: Module was compiled locally");
  } else {
    if (existsSync(prebuildsDir)) {
      console.log("\n x64 detected: Module loaded from prebuilds");
    } else {
      console.log(
        "\n💡 x64 detected: Module was compiled (no prebuilds available)"
      );
    }
  }
} catch (error) {
  console.error("\nFailed to load module");
  console.error(error.message);
  process.exit(1);
}

console.log("\nInstallation test complete!\n");

// Show next steps
console.log("Next steps:");
console.log('   - Run "npm test" to verify functionality');
console.log("   - Check PREBUILDS.md for prebuild generation");
if (!existsSync(prebuildsDir)) {
  console.log('   - Run "npm run prebuildify" to generate prebuilds');
}
console.log("");
