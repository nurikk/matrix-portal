from pathlib import Path

Import("env")

prescale = env.GetProjectOption("custom_protomatter_s3_lcd_clk_prescale", None)
if prescale:
    header = (
        Path(env.subst("$PROJECT_LIBDEPS_DIR"))
        / env.subst("$PIOENV")
        / "Adafruit Protomatter"
        / "src"
        / "arch"
        / "esp32-s3.h"
    )

    if header.exists():
        text = header.read_text()
        patched = []
        changed = False
        for line in text.splitlines():
            if line.startswith("#define LCD_CLK_PRESCALE "):
                patched.append(
                    f"#define LCD_CLK_PRESCALE {prescale} // Patched by PlatformIO env."
                )
                changed = True
            else:
                patched.append(line)

        if changed:
            header.write_text("\n".join(patched) + "\n")
            print(f"Patched Protomatter ESP32-S3 LCD clock prescale to {prescale}")
    else:
        print(f"Protomatter ESP32-S3 header not found yet: {header}")
