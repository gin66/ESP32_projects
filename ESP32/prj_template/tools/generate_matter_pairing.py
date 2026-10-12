#!/usr/bin/env python3
"""
Generate unique Matter pairing codes for ESP32 devices.
Run during build: python tools/generate_matter_pairing.py <project_name>
Creates matter_pairing.h with device-specific pairing codes.
"""

import random
import os
import sys


def generate_passcode():
    """Generate a valid Matter passcode (1 to 99999998)."""
    while True:
        code = random.randint(1, 99999998)
        # Avoid weak passcodes (Matter spec)
        weak_codes = [123456, 111111, 121212, 123123, 11111111]
        if code not in weak_codes:
            return code


def generate_discriminator():
    """Generate a valid Matter discriminator (12-bit, 0-4095)."""
    return random.randint(0, 4095)


def main():
    if len(sys.argv) < 2:
        print("Usage: python generate_matter_pairing.py <project_name>")
        sys.exit(1)

    project_name = sys.argv[1]

    # Use project name hash as seed for reproducible codes per project
    seed = sum(ord(c) for c in project_name)
    random.seed(seed)

    passcode = generate_passcode()
    discriminator = generate_discriminator()

    content = f"""// Auto-generated - do not edit
#pragma once

#define MATTER_DISCRIMINATOR {discriminator}
#define MATTER_PASSCODE "{passcode}"
"""

    output_path = f"{project_name}/include/matter_pairing.h"
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, "w") as f:
        f.write(content)

    print(f"Generated matter_pairing.h for {project_name}")
    print(f"  Discriminator: {discriminator}")
    print(f"  Passcode: {passcode}")


if __name__ == "__main__":
    main()
