#include <fstream>
#include <sstream>
#include "model.h"

/**
 * Model 类构造函数
 * 从 OBJ 文件加载 3D 模型数据，包括顶点、法线、纹理坐标和面信息
 * 同时加载相关的纹理贴图（漫反射贴图、法线贴图、高光贴图）
 *
 * @param filename OBJ 模型文件路径
 *
 * 工作流程：
 * 1. 逐行解析 OBJ 文件
 * 2. 识别 "v "（顶点）、"vn"（法线）、"vt"（纹理坐标）、"f "（面）前缀
 * 3. 存储顶点、法线、纹理坐标到对应容器
 * 4. 解析面信息，建立顶点、纹理、法线索引的对应关系
 * 5. 自动加载与模型同名的纹理贴图文件
 */
Model::Model(const std::string filename) {
    std::ifstream in;
    in.open(filename, std::ifstream::in);
    if (in.fail()) return;
    std::string line;
    while (!in.eof()) {
        std::getline(in, line);
        std::istringstream iss(line.c_str());
        char trash;
        if (!line.compare(0, 2, "v ")) {
            iss >> trash;
            vec4 v = {0,0,0,1};
            for (int i : {0,1,2}) iss >> v[i];
            verts.push_back(v);
        } else if (!line.compare(0, 3, "vn ")) {
            iss >> trash >> trash;
            vec4 n;
            for (int i : {0,1,2}) iss >> n[i];
            norms.push_back(normalized(n));
        } else if (!line.compare(0, 3, "vt ")) {
            iss >> trash >> trash;
            vec2 uv;
            for (int i : {0,1}) iss >> uv[i];
            tex.push_back({uv.x, 1-uv.y});
        } else if (!line.compare(0, 2, "f ")) {
            int f,t,n, cnt = 0;
            iss >> trash;
            while (iss >> f >> trash >> t >> trash >> n) {
                facet_vrt.push_back(--f);
                facet_tex.push_back(--t);
                facet_nrm.push_back(--n);
                cnt++;
            }
            if (3!=cnt) {
                std::cerr << "Error: the obj file is supposed to be triangulated" << std::endl;
                return;
            }
        }
    }
    std::cerr << "# v# " << nverts() << " f# "  << nfaces() << std::endl;
    auto load_texture = [&filename](const std::string suffix, TGAImage &img) {
        size_t dot = filename.find_last_of(".");
        if (dot==std::string::npos) return;
        std::string texfile = filename.substr(0,dot) + suffix;
        std::cerr << "texture file " << texfile << " loading " << (img.read_tga_file(texfile.c_str()) ? "ok" : "failed") << std::endl;
    };
    load_texture("_diffuse.tga",    diffusemap );
    load_texture("_nm_tangent.tga", normalmap);
    load_texture("_spec.tga",       specularmap);
}

/**
 * 返回模型中的顶点数量
 *
 * @return 顶点数组 verts 的大小
 */
int Model::nverts() const { return verts.size(); }
/**
 * 返回模型中的面（三角形）数量
 * 由于OBJ文件被假定为已三角化，每个面由3个顶点组成
 *
 * @return 面数量 = 顶点索引数组 facet_vrt 的大小除以3
 */
int Model::nfaces() const { return facet_vrt.size()/3; }

/**
 * 通过顶点索引获取单个顶点坐标
 *
 * @param i 顶点在 verts 数组中的索引
 * @return 第i个顶点的齐次坐标 (x,y,z,w)
 */
vec4 Model::vert(const int i) const {
    return verts[i];
}

/**
 * 获取指定面的第n个顶点坐标
 * 通过面索引和顶点在面中的位置查找顶点
 *
 * @param iface   面索引（第几个三角形）
 * @param nthvert 顶点在面中的位置（0,1,2 对应三角形的三个顶点）
 * @return 对应顶点的齐次坐标
 */
vec4 Model::vert(const int iface, const int nthvert) const {
    return verts[facet_vrt[iface*3+nthvert]];
}

/**
 * 获取指定面的第n个顶点的法线向量
 * 从加载的OBJ法线数据中查找对应法线
 *
 * @param iface   面索引
 * @param nthvert 顶点在面中的位置（0,1,2）
 * @return 归一化的法线向量（齐次坐标，w=0表示方向向量）
 */
vec4 Model::normal(const int iface, const int nthvert) const {
    return norms[facet_nrm[iface*3+nthvert]];
}

/**
 * 从法线贴图（normal map）中采样法线向量
 * 将纹理坐标映射到法线贴图像素，解码RGB值为法线向量
 * 法线贴图存储切线空间法线，需要从[0,255]范围转换到[-1,1]范围
 *
 * @param uv 纹理坐标，范围[0,1]
 * @return 从法线贴图采样的归一化法线向量
 */
vec4 Model::normal(const vec2 &uv) const {
    TGAColor c = normalmap.get(uv[0]*normalmap.width(), uv[1]*normalmap.height());
    return normalized(vec4{(double)c[2],(double)c[1],(double)c[0],0}*2./255. - vec4{1,1,1,0});
}

/**
 * 获取指定面的第n个顶点的纹理坐标
 *
 * @param iface   面索引
 * @param nthvert 顶点在面中的位置（0,1,2）
 * @return 纹理坐标 (u,v)，其中v坐标已翻转（OBJ格式与图像坐标系差异）
 */
vec2 Model::uv(const int iface, const int nthvert) const {
    return tex[facet_tex[iface*3+nthvert]];
}

/**
 * 获取漫反射贴图（diffuse map）引用
 * 漫反射贴图存储模型表面的基础颜色
 *
 * @return 漫反射贴图的常量引用
 */
const TGAImage& Model::diffuse()  const { return diffusemap;  }
/**
 * 获取高光贴图（specular map）引用
 * 高光贴图存储模型表面的镜面反射强度
 *
 * @return 高光贴图的常量引用
 */
const TGAImage& Model::specular() const { return specularmap; }

