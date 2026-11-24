/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

use iceoryx2::prelude::*;
use std::time::{Duration, Instant};
#[allow(unused_imports, dead_code)]
mod generated {
    include!(concat!(
        env!("FRAME_ACCESS_GENERATED_DIR"),
        "/frame_reply_generated.rs"
    ));
}
use generated::opk::frame_access::v_1::{FrameReply, ReplyStatus};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut config = Config::default();
    config
        .global
        .set_root_path(&std::env::var("PROBE_RUNTIME")?.as_str().try_into()?);
    config.global.prefix = "deml2629mix_".try_into()?;
    let node = NodeBuilder::new()
        .config(&config)
        .create::<ipc::Service>()?;
    let service = node
        .service_builder(&"DEML2629/mixed".try_into()?)
        .request_response::<u64, Flatbuffer<FrameReply>>()
        .response_flatbuffer_schema_path(&std::env::var("PROBE_SCHEMA_RUST")?.as_str().try_into()?)
        .open()?;
    let client = service.client_builder().create()?;
    let mut retained = None;
    for index in 0..10 {
        let pending = client.send_copy(if index == 7 {
            0
        } else if index == 8 {
            2
        } else {
            1
        })?;
        let deadline = Instant::now() + Duration::from_secs(5);
        let response = loop {
            if let Some(response) = pending.receive()? {
                break response;
            }
            if Instant::now() > deadline {
                return Err("response deadline expired".into());
            }
            std::thread::sleep(Duration::from_millis(1));
        };
        drop(pending);
        let root = response.payload_root()?;
        if index >= 8 {
            assert_eq!(
                root.status(),
                if index == 8 {
                    ReplyStatus::InvalidRequest
                } else {
                    ReplyStatus::NotReady
                }
            );
            assert!(
                root.frame().is_none() && root.sample().is_none() && root.opk_results().is_none()
            );
            continue;
        }
        assert_eq!(root.status(), ReplyStatus::Ok);
        let sample = root.sample().unwrap();
        assert_eq!(sample.sequence(), index);
        assert_eq!(sample.pts_ns(), if index % 2 == 0 { Some(0) } else { None });
        let bytes = root.opk_results().unwrap();
        let nested = open_perception_kit::Envelope::decode(bytes.bytes().to_vec())?;
        assert_eq!(nested.producer_sdk_name(), "open_perception_kit");
        assert_eq!(nested.producer_sdk_version(), "1.0.0");
        assert_eq!(
            nested.producer_identity(),
            open_perception_kit::ProducerIdentityStatus::ExactMatch
        );
        if index == 7 {
            assert!(root.frame().is_none());
            continue;
        }
        let frame = root.frame().unwrap();
        let dimension = if index == 6 { 128 } else { 3 };
        assert_eq!((frame.width(), frame.height()), (dimension, dimension));
        assert_eq!(
            frame.format().0 as u64,
            if index < 6 { index + 1 } else { 1 }
        );
        let lengths = match index {
            0 => vec![36],
            1 => vec![27],
            2 => vec![9],
            3 => vec![9, 4, 4],
            4 => vec![9, 8],
            5 => vec![24],
            6 => vec![65536],
            _ => unreachable!(),
        };
        assert_eq!(frame.planes().len(), lengths.len());
        for (p, expected) in lengths.into_iter().enumerate() {
            let plane = frame.planes().get(p);
            assert_eq!(plane.data().len(), expected);
            for (i, value) in plane.data().iter().enumerate() {
                let expected = if index == 5 && i % 8 == 6 {
                    0
                } else {
                    ((i + p) % 251) as u8
                };
                assert_eq!(value, expected);
            }
        }
        if index == 6 {
            retained = Some(response);
        }
    }
    drop(client);
    let stopped = std::path::PathBuf::from(std::env::var("PROBE_STOPPED_FILE")?);
    let deadline = Instant::now() + Duration::from_secs(5);
    while !stopped.exists() {
        if Instant::now() > deadline {
            return Err("server exit was not confirmed".into());
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    let retained = retained.unwrap();
    let root = retained.payload_root()?;
    let data = root.frame().unwrap().planes().get(0).data();
    assert_eq!(data.len(), 65536);
    assert!(data
        .iter()
        .enumerate()
        .all(|(i, value)| value == (i % 251) as u8));
    println!("PASS 10 mixed-version replies: all formats, metadata, errors, PTS, retained native response");
    Ok(())
}
