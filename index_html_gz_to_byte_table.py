import gzip

# Wczytaj skompresowany plik
with open("index.html.gz", "rb") as f:
    data = f.read()

# Utwórz tablicę bajtów z 16 bajtami na wiersz
lines = []
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    line = ', '.join(f"0x{byte:02x}" for byte in chunk)
    lines.append(f"    {line}")

# Scal linie w jedną tablicę
byte_array = ",\n".join(lines)

# Wygeneruj kod tablicy
output = f"const uint8_t index_html_gz[] = {{\n{byte_array}\n}};\n"
output += f"const size_t index_html_gz_len = {len(data)};"

# Zapisz wynik do pliku
with open("index_html_gz.h", "w") as header_file:
    header_file.write(output)

print("Tablica bajtów została wygenerowana jako 'index_html_gz.h'.")
