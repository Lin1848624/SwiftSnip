#pragma once

// 查询开机自启是否已开启（且与当前 exe 路径一致）
bool AutoStartIsEnabled();

// 设置开机自启
bool AutoStartSet(bool enabled);

