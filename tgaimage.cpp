#include <iostream>
#include <cstring>
#include "tgaimage.h"

/**
 * TGAImage 类构造函数
 * 创建指定尺寸和位深的TGA图像，并用给定颜色初始化所有像素
 *
 * @param w   图像宽度（像素）
 * @param h   图像高度（像素）
 * @param bpp 每像素字节数（1=灰度, 3=RGB, 4=RGBA）
 * @param c   初始填充颜色
 *
 * 工作流程：
 * 1. 初始化宽度、高度、位深成员变量
 * 2. 分配像素数据缓冲区（w*h*bpp字节）
 * 3. 遍历所有像素，用指定颜色填充
 */
TGAImage::TGAImage(const int w, const int h, const int bpp, TGAColor c) : w(w), h(h), bpp(bpp), data(w*h*bpp, 0) {
    for (int j=0; j<h; j++)
        for (int i=0; i<w; i++)
            set(i, j, c);
}

/**
 * 从TGA文件读取图像数据
 * 支持未压缩（RAW）和RLE（行程编码）格式的TGA文件
 *
 * @param filename TGA文件路径
 * @return 成功读取返回true，失败返回false
 *
 * 工作流程：
 * 1. 以二进制模式打开文件
 * 2. 读取TGA文件头，验证格式有效性
 * 3. 根据数据编码类型（RAW或RLE）加载像素数据
 * 4. 根据图像描述符调整图像方向（垂直/水平翻转）
 * 5. 输出图像尺寸和位深信息
 */
bool TGAImage::read_tga_file(const std::string filename) {
    std::ifstream in;
    in.open(filename, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "can't open file " << filename << "\n";
        return false;
    }
    TGAHeader header;
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!in.good()) {
        std::cerr << "an error occured while reading the header\n";
        return false;
    }
    w   = header.width;
    h   = header.height;
    bpp = header.bitsperpixel>>3;
    if (w<=0 || h<=0 || (bpp!=GRAYSCALE && bpp!=RGB && bpp!=RGBA)) {
        std::cerr << "bad bpp (or width/height) value\n";
        return false;
    }
    size_t nbytes = bpp*w*h;
    data = std::vector<std::uint8_t>(nbytes, 0);
    if (3==header.datatypecode || 2==header.datatypecode) {
        in.read(reinterpret_cast<char *>(data.data()), nbytes);
        if (!in.good()) {
            std::cerr << "an error occured while reading the data\n";
            return false;
        }
    } else if (10==header.datatypecode||11==header.datatypecode) {
        if (!load_rle_data(in)) {
            std::cerr << "an error occured while reading the data\n";
            return false;
        }
    } else {
        std::cerr << "unknown file format " << (int)header.datatypecode << "\n";
        return false;
    }
    if (!(header.imagedescriptor & 0x20))
        flip_vertically();
    if (header.imagedescriptor & 0x10)
        flip_horizontally();
    std::cerr << w << "x" << h << "/" << bpp*8 << "\n";
    return true;
}

/**
 * 加载RLE（Run-Length Encoding）压缩的TGA像素数据
 * RLE编码将连续的相同像素压缩为"run"数据包，不同像素存储为"raw"数据包
 *
 * @param in 已打开的输入文件流（定位在像素数据开始处）
 * @return 成功加载返回true，失败返回false
 *
 * RLE格式解析：
 * - 数据包头字节：最高位为0表示raw包，为1表示run包
 * - raw包：头字节低7位+1 = 后面跟随的像素数量，每个像素独立存储
 * - run包：头字节低7位+1 = 重复像素数量，后面跟随一个像素值重复多次
 */
bool TGAImage::load_rle_data(std::ifstream &in) {
    size_t pixelcount = w*h;
    size_t currentpixel = 0;
    size_t currentbyte  = 0;
    TGAColor colorbuffer;
    do {
        std::uint8_t chunkheader = 0;
        chunkheader = in.get();
        if (!in.good()) {
            std::cerr << "an error occured while reading the data\n";
            return false;
        }
        if (chunkheader<128) {
            chunkheader++;
            for (int i=0; i<chunkheader; i++) {
                in.read(reinterpret_cast<char *>(colorbuffer.bgra), bpp);
                if (!in.good()) {
                    std::cerr << "an error occured while reading the header\n";
                    return false;
                }
                for (int t=0; t<bpp; t++)
                    data[currentbyte++] = colorbuffer.bgra[t];
                currentpixel++;
                if (currentpixel>pixelcount) {
                    std::cerr << "Too many pixels read\n";
                    return false;
                }
            }
        } else {
            chunkheader -= 127;
            in.read(reinterpret_cast<char *>(colorbuffer.bgra), bpp);
            if (!in.good()) {
                std::cerr << "an error occured while reading the header\n";
                return false;
            }
            for (int i=0; i<chunkheader; i++) {
                for (int t=0; t<bpp; t++)
                    data[currentbyte++] = colorbuffer.bgra[t];
                currentpixel++;
                if (currentpixel>pixelcount) {
                    std::cerr << "Too many pixels read\n";
                    return false;
                }
            }
        }
    } while (currentpixel < pixelcount);
    return true;
}

/**
 * 将图像数据写入TGA文件
 * 可选择输出格式（RAW或RLE压缩）和图像方向
 *
 * @param filename 输出文件路径
 * @param vflip    是否垂直翻转图像（true=底部原点，false=顶部原点）
 * @param rle      是否使用RLE压缩
 * @return 成功写入返回true，失败返回false
 *
 * 工作流程：
 * 1. 打开输出文件（二进制模式）
 * 2. 填充TGA文件头信息（尺寸、位深、编码类型）
 * 3. 根据rle参数选择RAW或RLE编码写入像素数据
 * 4. 写入开发区域和扩展区域引用（空）
 * 5. 写入TGA文件尾标识"TRUEVISION-XFILE."
 */
bool TGAImage::write_tga_file(const std::string filename, const bool vflip, const bool rle) const {
    constexpr std::uint8_t developer_area_ref[4] = {0, 0, 0, 0};
    constexpr std::uint8_t extension_area_ref[4] = {0, 0, 0, 0};
    constexpr std::uint8_t footer[18] = {'T','R','U','E','V','I','S','I','O','N','-','X','F','I','L','E','.','\0'};
    std::ofstream out;
    out.open(filename, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "can't open file " << filename << "\n";
        return false;
    }
    TGAHeader header = {};
    header.bitsperpixel = bpp<<3;
    header.width  = w;
    header.height = h;
    header.datatypecode = (bpp==GRAYSCALE ? (rle?11:3) : (rle?10:2));
    header.imagedescriptor = vflip ? 0x00 : 0x20; // top-left or bottom-left origin
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    if (!out.good()) goto err;
    if (!rle) {
        out.write(reinterpret_cast<const char *>(data.data()), w*h*bpp);
        if (!out.good()) goto err;
    } else if (!unload_rle_data(out)) goto err;
    out.write(reinterpret_cast<const char *>(developer_area_ref), sizeof(developer_area_ref));
    if (!out.good()) goto err;
    out.write(reinterpret_cast<const char *>(extension_area_ref), sizeof(extension_area_ref));
    if (!out.good()) goto err;
    out.write(reinterpret_cast<const char *>(footer), sizeof(footer));
    if (!out.good()) goto err;
    return true;
err:
    std::cerr << "can't dump the tga file\n";
    return false;
}

/**
 * 将图像数据编码为RLE格式并写入输出流
 * 实现TGA RLE压缩算法，将连续相同像素编码为run包，不同像素编码为raw包
 *
 * @param out 输出文件流
 * @return 成功编码并写入返回true，失败返回false
 *
 * 编码算法：
 * 1. 遍历所有像素，查找连续的相同像素序列（run）或不同像素序列（raw）
 * 2. 每个数据包最多包含128个像素
 * 3. run包：头字节 = 重复次数-1 | 0x80，后跟一个像素值
 * 4. raw包：头字节 = 像素数量-1，后跟相应数量的像素值
 */
bool TGAImage::unload_rle_data(std::ofstream &out) const {
    const std::uint8_t max_chunk_length = 128;
    size_t npixels = w*h;
    size_t curpix = 0;
    while (curpix<npixels) {
        size_t chunkstart = curpix*bpp;
        size_t curbyte = curpix*bpp;
        std::uint8_t run_length = 1;
        bool raw = true;
        while (curpix+run_length<npixels && run_length<max_chunk_length) {
            bool succ_eq = true;
            for (int t=0; succ_eq && t<bpp; t++)
                succ_eq = (data[curbyte+t]==data[curbyte+t+bpp]);
            curbyte += bpp;
            if (1==run_length)
                raw = !succ_eq;
            if (raw && succ_eq) {
                run_length--;
                break;
            }
            if (!raw && !succ_eq)
                break;
            run_length++;
        }
        curpix += run_length;
        out.put(raw ? run_length-1 : run_length+127);
        if (!out.good()) return false;
        out.write(reinterpret_cast<const char *>(data.data()+chunkstart), (raw?run_length*bpp:bpp));
        if (!out.good()) return false;
    }
    return true;
}

/**
 * 获取指定位置像素的颜色值
 * 进行边界检查，坐标越界时返回默认颜色（全0）
 *
 * @param x 像素X坐标（从左到右，0-based）
 * @param y 像素Y坐标（从上到下，0-based）
 * @return 对应位置的TGAColor结构，包含RGBA/BGRA值（根据bpp）
 *
 * 内存布局：
 * 像素数据按行优先存储：data[(x + y*width) * bpp + channel]
 * channel顺序：BGR(A) 或 灰度
 */
TGAColor TGAImage::get(const int x, const int y) const {
    if (!data.size() || x<0 || y<0 || x>=w || y>=h) return {};
    TGAColor ret = {0, 0, 0, 0, bpp};
    const std::uint8_t *p = data.data()+(x+y*w)*bpp;
    for (int i=bpp; i--; ret.bgra[i] = p[i]);
    return ret;
}

/**
 * 设置指定位置像素的颜色值
 * 进行边界检查，坐标越界时静默返回（无操作）
 *
 * @param x 像素X坐标
 * @param y 像素Y坐标
 * @param c 要设置的TGAColor颜色值
 *
 * 实现细节：
 * 使用memcpy快速复制bpp个字节到像素数据缓冲区
 * 不进行颜色空间转换，直接存储提供的颜色分量
 */
void TGAImage::set(int x, int y, const TGAColor &c) {
    if (!data.size() || x<0 || y<0 || x>=w || y>=h) return;
    memcpy(data.data()+(x+y*w)*bpp, c.bgra, bpp);
}

/**
 * 水平翻转图像（镜像翻转）
 * 将图像左右对调，用于调整TGA文件的坐标系
 *
 * 算法：
 * 对于每一行，交换第i列和第(width-1-i)列的像素
 * 只遍历左半部分列，与对应右半部分交换
 * 对每个像素的所有颜色通道分别交换
 */
void TGAImage::flip_horizontally() {
    for (int i=0; i<w/2; i++)
        for (int j=0; j<h; j++)
            for (int b=0; b<bpp; b++)
                std::swap(data[(i+j*w)*bpp+b], data[(w-1-i+j*w)*bpp+b]);
}

/**
 * 垂直翻转图像（上下翻转）
 * 将图像上下对调，用于调整TGA文件的坐标系
 *
 * 算法：
 * 对于每一列，交换第j行和第(height-1-j)行的像素
 * 只遍历上半部分行，与对应下半部分交换
 * 对每个像素的所有颜色通道分别交换
 */
void TGAImage::flip_vertically() {
    for (int i=0; i<w; i++)
        for (int j=0; j<h/2; j++)
            for (int b=0; b<bpp; b++)
                std::swap(data[(i+j*w)*bpp+b], data[(i+(h-1-j)*w)*bpp+b]);
}

/**
 * 获取图像宽度
 *
 * @return 图像宽度（像素）
 */
int TGAImage::width() const {
    return w;
}

/**
 * 获取图像高度
 *
 * @return 图像高度（像素）
 */
int TGAImage::height() const {
    return h;
}

