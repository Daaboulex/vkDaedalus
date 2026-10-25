#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_number(FILE* file, long* out)
{
    int c = fgetc(file);
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '#')
    {
        if (c == '#')
            while (c != '\n' && c != EOF)
                c = fgetc(file);
        c = fgetc(file);
    }
    if (c < '0' || c > '9')
        return 0;
    long value = 0;
    while (c >= '0' && c <= '9')
    {
        value = value * 10 + (c - '0');
        if (value > 1000000)
            return 0;
        c = fgetc(file);
    }
    *out = value;
    return 1;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: pixel-probe <frame.ppm> <square side>\n");
        return 2;
    }
    const long side = atol(argv[2]);
    FILE*      file = fopen(argv[1], "rb");
    if (!file)
    {
        fprintf(stderr, "pixel-probe: cannot open %s\n", argv[1]);
        return 1;
    }
    char magic[3] = {0};
    long width = 0, height = 0, maxval = 0;
    if (fread(magic, 1, 2, file) != 2 || strcmp(magic, "P6") != 0 || !next_number(file, &width) || !next_number(file, &height)
        || !next_number(file, &maxval) || maxval != 255 || side <= 0 || width < side || height < side)
    {
        fprintf(stderr, "pixel-probe: %s is not an 8-bit P6 image at least %ld pixels square\n", argv[1], side);
        fclose(file);
        return 1;
    }
    const long     pixels = width * height;
    unsigned char* rgb    = malloc((size_t) pixels * 3);
    if (!rgb || fread(rgb, 3, (size_t) pixels, file) != (size_t) pixels)
    {
        fprintf(stderr, "pixel-probe: %s ends before its %ldx%ld pixels\n", argv[1], width, height);
        free(rgb);
        fclose(file);
        return 1;
    }
    fclose(file);

    unsigned long long sum[3] = {0, 0, 0}, squares[3] = {0, 0, 0};
    const long         left = (width - side) / 2, top = (height - side) / 2;
    for (long y = top; y < top + side; y++)
        for (long x = left; x < left + side; x++)
            for (int c = 0; c < 3; c++)
            {
                const unsigned long long value = rgb[(y * width + x) * 3 + c];
                sum[c] += value;
                squares[c] += value * value;
            }
    free(rgb);
    const unsigned long long n = (unsigned long long) side * (unsigned long long) side;
    printf("%llu %llu %llu %ldx%ld %llu %llu %llu\n", sum[0] / n, sum[1] / n, sum[2] / n, width, height,
           squares[0] / n - (sum[0] / n) * (sum[0] / n), squares[1] / n - (sum[1] / n) * (sum[1] / n),
           squares[2] / n - (sum[2] / n) * (sum[2] / n));
    return 0;
}
