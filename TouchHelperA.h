#pragma once

#include <linux/input.h>
#include <vector>
#include <functional>
#include <cstdint>
#include "VectorStruct.h"

namespace Touch {

    struct touchObj {
        Vector2 pos{};
        int id = 0;
        bool isDown = false;
    };

    struct Device {
        int fd;
        float S2TX;
        float S2TY;
        input_absinfo absX, absY;
        touchObj Finger[10];

        Device() {
            memset((void *)this, 0, sizeof(*this));
        }
    };

    // AutoAim 用的真实手指快照
    struct FingerInfo {
        int device = -1;
        int slot = -1;
        int id = 0;

        float x = 0.0f;
        float y = 0.0f;

        bool isDown = false;

        // 每次真实 DOWN 都递增。
        // 用它区分“之前已经存在的手指”和“刚按下的技能手指”。
        uint64_t downSerial = 0;
    };

    bool Init(const Vector2 &s, bool p_readOnly);
    void Close();

    void Down(float x, float y);
    void Move(float x, float y);
    void Up();
    void Move(touchObj *touch, float x, float y);

    void Upload();

    void SetCallBack(
        const std::function<void(std::vector<Device> *)> &cb
    );

    Vector2 Touch2Screen(const Vector2 &coord);
    Vector2 GetScale();

    void setOrientation(int orientation);
    void setOtherTouch(bool p_otherTouch);

    // =========================================================
    // AutoAim 专用接口
    // =========================================================

    // 获取当前所有真实按下的手指。
    //
    // 返回值：
    //   >=0 : 当前有效手指数
    //   0   : 没有手指
    //
    // 注意：
    //   这个接口返回的是“真实触摸设备里的手指”，
    //   不是 AutoAim 创建的虚拟手指。
    int GetActiveFingers(FingerInfo *out, int maxCount);

    // 按“屏幕坐标”移动一个已经存在的真实手指。
    //
    // device / slot：
    //   来自 GetActiveFingers()
    //
    // x / y：
    //   游戏屏幕坐标，不需要 AutoAim 自己处理 orientation。
    //
    // 返回 false：
    //   手指已经不存在、索引无效、Touch 尚未初始化等。
    bool MoveExistingScreen(int device, int slot, float x, float y);

    // 判断指定真实手指是否仍然按下。
    bool IsFingerDown(int device, int slot);

} // namespace Touch
