// Copyright (C) 2026 VP_xudon
// SPDX-License-Identifier: GPL-3.0-or-later
// See LICENSE in the project root for the full license text.

// ============================================================
// lib/std_libs.hpp
//
// Aggregator for every C++-backed standard library. The interpreter
// includes this file exactly once; each library self-registers through
// register_native_lib at static-initialization time, so the engine never
// names the libraries explicitly. New C++-backed libraries are added here
// only (not into builtin.hpp), keeping them arranged like the interpreted
// .synl libraries.
// 每一个以 C++ 为底层的标准库的聚合器。解释器只须包含本文件一次；
// 各库在静态初始化时经 register_native_lib 自注册，故引擎无需显式指名。
// 新增 C++ 底层库只在此处添加（而非塞进 builtin.hpp），使它们与解释型
// .synl 标准库的排列方式一致。
// ============================================================

#pragma once

#include "file.hpp"
#include "system.hpp"
#include "maths.hpp"
#include "async.hpp"
#include "hash.hpp"
#include "structs.hpp"
#include "re.hpp"
#include "io.hpp"
#include "assert.hpp"
#include "sugar.hpp"
