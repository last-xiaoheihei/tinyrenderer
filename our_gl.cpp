#include <algorithm>
#include "our_gl.h"

mat<4,4> ModelView, Viewport, Perspective; // "OpenGL" state matrices
std::vector<double> zbuffer;               // depth buffer

/**
 * 构建视图矩阵（ModelView矩阵）
 * 将世界坐标系转换为观察者（相机）坐标系
 *
 * @param eye    相机位置（世界坐标）
 * @param center 相机看向的目标点（世界坐标）
 * @param up     相机上方向向量（世界坐标）
 *
 * 数学原理：
 * 1. 计算观察方向：n = normalized(eye - center)
 * 2. 计算右方向：l = normalized(cross(up, n))
 * 3. 计算实际上方向：m = normalized(cross(n, l))
 * 4. 构建旋转矩阵：将世界坐标轴对齐到相机坐标轴
 * 5. 应用平移：将相机位置移动到原点
 *
 * 结果矩阵：ModelView = Rotation * Translation
 */
void lookat(const vec3 eye, const vec3 center, const vec3 up) {
    vec3 n = normalized(eye-center);
    vec3 l = normalized(cross(up,n));
    vec3 m = normalized(cross(n, l));
    ModelView = mat<4,4>{{{l.x,l.y,l.z,0}, {m.x,m.y,m.z,0}, {n.x,n.y,n.z,0}, {0,0,0,1}}} *
                mat<4,4>{{{1,0,0,-center.x}, {0,1,0,-center.y}, {0,0,1,-center.z}, {0,0,0,1}}};
}

/**
 * 构建透视投影矩阵
 * 将观察坐标系转换为裁剪坐标系，实现透视效果
 *
 * @param f 相机到投影平面的距离（焦距）
 *         控制透视变形程度，值越小透视效果越强
 *
 * 矩阵形式：
 * [ 1  0  0   0 ]
 * [ 0  1  0   0 ]
 * [ 0  0  1   0 ]
 * [ 0  0 -1/f 1 ]
 *
 * 作用：将z坐标映射为 w' = z - f，用于后续的透视除法
 * 透视除法：将齐次坐标除以w分量，实现近大远小效果
 */
void init_perspective(const double f) {
    Perspective = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0, -1/f,1}}};
}

/**
 * 构建视口变换矩阵
 * 将标准化设备坐标（NDC）转换为屏幕像素坐标
 *
 * @param x 视口左下角X坐标（像素）
 * @param y 视口左下角Y坐标（像素）
 * @param w 视口宽度（像素）
 * @param h 视口高度（像素）
 *
 * 变换过程：
 * 1. NDC范围：[-1,1] × [-1,1] × [0,1]（x,y,z）
 * 2. 缩放：将x从[-1,1]映射到[0,w]，y从[-1,1]映射到[0,h]
 * 3. 平移：将原点从NDC中心移到视口左下角
 *
 * 矩阵形式：
 * [ w/2   0    0   x+w/2 ]
 * [ 0    h/2   0   y+h/2 ]
 * [ 0     0    1     0    ]
 * [ 0     0    0     1    ]
 */
void init_viewport(const int x, const int y, const int w, const int h) {
    Viewport = {{{w/2., 0, 0, x+w/2.}, {0, h/2., 0, y+h/2.}, {0,0,1,0}, {0,0,0,1}}};
}

/**
 * 初始化深度缓冲区（z-buffer）
 * 分配指定尺寸的深度缓冲区，并用远距离值初始化
 *
 * @param width  缓冲区宽度（与帧缓冲宽度相同）
 * @param height 缓冲区高度（与帧缓冲高度相同）
 *
 * 深度值含义：
 * - 使用右手坐标系，z轴指向屏幕内
 * - 较小的z值表示更靠近观察者（更近）
 * - 初始化为-1000.0（很远的距离），确保任何物体都能通过深度测试
 *
 * 内存布局：
 * zbuffer[x + y*width] 存储对应像素的深度值
 */
void init_zbuffer(const int width, const int height) {
    zbuffer = std::vector(width*height, -1000.);
}

/**
 * 三角形光栅化函数
 * 将3D空间中的三角形转换为2D屏幕像素，进行深度测试和着色
 *
 * @param clip       裁剪空间中的三角形（三个齐次坐标顶点）
 * @param shader     着色器对象，实现片段着色逻辑
 * @param framebuffer 帧缓冲图像，用于输出最终像素颜色
 *
 * 算法步骤：
 * 1. 坐标变换：裁剪坐标 → NDC → 屏幕坐标
 * 2. 背面剔除：通过三角形面积符号判断面向方向
 * 3. 包围盒计算：确定需要遍历的像素范围
 * 4. 像素遍历：对包围盒内每个像素计算重心坐标
 * 5. 内部测试：通过重心坐标判断像素是否在三角形内
 * 6. 深度测试：插值深度值，与深度缓冲比较
 * 7. 片段着色：调用着色器计算像素颜色
 * 8. 缓冲更新：更新深度缓冲和帧缓冲
 *
 * 关键技术：
 * - 透视校正插值：使用裁剪空间重心坐标进行属性插值
 * - 早期深度测试：在着色前丢弃不可见片段
 * - OpenMP并行化：加速像素遍历过程
 */
void rasterize(const Triangle &clip, const IShader &shader, TGAImage &framebuffer) {
    vec4 ndc[3]    = { clip[0]/clip[0].w, clip[1]/clip[1].w, clip[2]/clip[2].w };                // normalized device coordinates
    vec2 screen[3] = { (Viewport*ndc[0]).xy(), (Viewport*ndc[1]).xy(), (Viewport*ndc[2]).xy() }; // screen coordinates

    mat<3,3> ABC = {{ {screen[0].x, screen[0].y, 1.}, {screen[1].x, screen[1].y, 1.}, {screen[2].x, screen[2].y, 1.} }};
    if (ABC.det()<1) return; // backface culling + discarding triangles that cover less than a pixel

    auto [bbminx,bbmaxx] = std::minmax({screen[0].x, screen[1].x, screen[2].x}); // bounding box for the triangle
    auto [bbminy,bbmaxy] = std::minmax({screen[0].y, screen[1].y, screen[2].y}); // defined by its top left and bottom right corners
#pragma omp parallel for
    for (int x=std::max<int>(bbminx, 0); x<=std::min<int>(bbmaxx, framebuffer.width()-1); x++) {         // clip the bounding box by the screen
        for (int y=std::max<int>(bbminy, 0); y<=std::min<int>(bbmaxy, framebuffer.height()-1); y++) {
            vec3 bc_screen = ABC.invert_transpose() * vec3{static_cast<double>(x), static_cast<double>(y), 1.}; // barycentric coordinates of {x,y} w.r.t the triangle
            vec3 bc_clip   = { bc_screen.x/clip[0].w, bc_screen.y/clip[1].w, bc_screen.z/clip[2].w };     // check https://github.com/ssloy/tinyrenderer/wiki/Technical-difficulties-linear-interpolation-with-perspective-deformations
            bc_clip = bc_clip / (bc_clip.x + bc_clip.y + bc_clip.z);
            if (bc_screen.x<0 || bc_screen.y<0 || bc_screen.z<0) continue; // negative barycentric coordinate => the pixel is outside the triangle
            double z = bc_screen * vec3{ ndc[0].z, ndc[1].z, ndc[2].z };   // linear interpolation of the depth
            if (z <= zbuffer[x+y*framebuffer.width()]) continue;   // discard fragments that are too deep w.r.t the z-buffer
            auto [discard, color] = shader.fragment(bc_clip);
            if (discard) continue;                                 // fragment shader can discard current fragment
            zbuffer[x+y*framebuffer.width()] = z;                  // update the z-buffer
            framebuffer.set(x, y, color);                          // update the framebuffer
        }
    }
}

