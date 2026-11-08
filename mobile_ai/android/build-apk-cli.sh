#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="${ANDROID_HOME:-/usr/lib/android-sdk}"
PLATFORM="${ANDROID_PLATFORM:-android-23}"
BUILD_TOOLS="${ANDROID_BUILD_TOOLS:-29.0.3}"
if [[ -x /usr/lib/jvm/java-17-openjdk-amd64/bin/javac ]]; then
  JAVA=/usr/lib/jvm/java-17-openjdk-amd64/bin
elif [[ -n "${JAVA_HOME:-}" && -x "$JAVA_HOME/bin/javac" ]]; then
  JAVA="$JAVA_HOME/bin"
else
  JAVA="$(dirname "$(command -v javac)")"
fi
ANDROID_JAR="$SDK/platforms/$PLATFORM/android.jar"
TOOLS="$SDK/build-tools/$BUILD_TOOLS"
OUT="$ROOT/.cli-build"

test -x "$JAVA/javac"
test -f "$ANDROID_JAR"
test -x "$TOOLS/aapt"
test -x "$TOOLS/apksigner"
test -x "$TOOLS/zipalign"

rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/gen"

"$JAVA/javac" -source 7 -target 7 -Xlint:-options \
  -classpath "$ANDROID_JAR" -d "$OUT/classes" \
  "$ROOT/app/src/main/java/com/colibri/mobile/MainActivity.java" \
  "$ROOT/app/src/main/java/com/colibri/mobile/NativeEngine.java"

DX_CP="$(find /usr/share/java -maxdepth 1 -name '*.jar' -printf '%p:' | sed 's/:$//')"
java -cp "$DX_CP" com.android.dx.command.Main \
  --dex --output="$OUT/classes.dex" "$OUT/classes"

"$TOOLS/aapt" package -f \
  --version-code 1 --version-name 0.1.0 \
  -M "$ROOT/app/src/main/AndroidManifest.xml" \
  -S "$ROOT/app/src/main/res" -I "$ANDROID_JAR" \
  -J "$OUT/gen" -F "$OUT/unsigned.apk"
(cd "$OUT" && zip -q -u unsigned.apk classes.dex)
"$TOOLS/zipalign" -f 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

KEYSTORE="$OUT/debug.keystore"
if [[ ! -f "$KEYSTORE" ]]; then
  keytool -genkeypair -keystore "$KEYSTORE" -storepass android \
    -alias androiddebugkey -keypass android \
    -dname 'CN=Android Debug,O=Android,C=US' \
    -keyalg RSA -keysize 2048 -validity 10000 >/dev/null 2>&1
fi

"$TOOLS/apksigner" sign \
  --ks "$KEYSTORE" --ks-pass pass:android \
  --ks-key-alias androiddebugkey --key-pass pass:android \
  --out "$ROOT/app-debug.apk" "$OUT/aligned.apk"
"$TOOLS/apksigner" verify --verbose "$ROOT/app-debug.apk"
"$TOOLS/aapt" dump badging "$ROOT/app-debug.apk" | head -8
echo "APK: $ROOT/app-debug.apk"
