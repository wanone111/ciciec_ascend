from ciciec_ascend.character_voice_chat.audio.mic_input import MicrophoneInput


def main():
    print("[test] 程序启动")
    mic = MicrophoneInput(device=1, samplerate=16000, channels=1)
    print("[test] mic 已创建")
    audio = mic.record_seconds(duration=4, save_path="test_headset.wav")
    print("[test] 录音长度:", len(audio))
    print("[test] 测试完成")


if __name__ == "__main__":
    main()