/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
export type PayloadClass<T = unknown> = Function & {
    prototype: T;
};
export type TypeInfo = {
    name: string;
    root_type: string;
    qualified_root_type: string;
    file_identifier: string;
    decode: (blob: Uint8Array) => unknown;
    verify?: (blob: Uint8Array) => boolean;
};
export declare const _TYPE_REGISTRY: Map<bigint, TypeInfo>;
export declare const _CLASS_TO_ID: Map<PayloadClass<unknown>, bigint>;
