#!/usr/bin/env python3

import os
import sys
import re

def process_file(filename):

    basename = os.path.basename(filename)

    varname = re.sub(r'[^a-zA-Z0-9_]', '_', os.path.splitext(basename)[0])

    with open(filename, 'rb') as f:
        data = f.read()

    output = f"// Datas for {basename}\n"
    output += f"static const unsigned char {varname}_data[] = {{\n    "

    byte_count = 0
    for byte in data:
        output += f"0x{byte:02x}, "
        byte_count += 1
        if byte_count % 12 == 0:
            output += "\n    "
    
    output += "\n};\n"
    output += f"static const size_t {varname}_size = sizeof({varname}_data);\n\n"
    
    return output

def generate_header_file(directory):

    pattern = re.compile(r'(driveact|drivenoact|diskact)\.png')
    image_files = [os.path.join(directory, f) for f in os.listdir(directory) 
                  if pattern.match(f)]
    
    if not image_files:
        print(f"no icons found in {directory}")
        return None

    header_content = "/* header file auto generated for icons */\n"
    header_content += "#ifndef DISK_ICONS_H\n"
    header_content += "#define DISK_ICONS_H\n\n"
    header_content += "#include <stddef.h>\n\n"

    for file in image_files:
        header_content += process_file(file)
    
    header_content += "#endif /* DISK_ICONS_H */\n"

    with open("disk_icons.h", "w") as f:
        f.write(header_content)
    
    print(f"disk_icons.h generated:  {len(image_files)} icônes")
    return "disk_icons.h"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 convert_images.py /chemin/vers/les/icones/")
        sys.exit(1)
    
    icon_dir = sys.argv[1]
    if not os.path.isdir(icon_dir):
        print(f"Folder {icon_dir} doesn't exists")
        sys.exit(1)
    
    generate_header_file(icon_dir)
    