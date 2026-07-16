"""Factories shared by CLI scripts and future HTTP/WebSocket integration."""

from .config import DEFAULT_KB_DIR, VoiceChatSettings
from .core.dialogue_manager import DialogueManager
from .core.realtime_pipeline import RealtimeDialoguePipeline
from .rag.simple_kb import load_kb


def create_dialogue_manager(*, chat_fn=None, kb_dir=DEFAULT_KB_DIR):
    return DialogueManager(load_kb(kb_dir), chat_fn=chat_fn)


def create_realtime_pipeline(asr_engine, speaker, *, dialogue_manager=None, settings=None):
    settings = settings or VoiceChatSettings.from_env()
    dialogue_manager = dialogue_manager or create_dialogue_manager()
    return RealtimeDialoguePipeline(
        asr_engine=asr_engine,
        dialogue_manager=dialogue_manager,
        speaker=speaker,
        silence_threshold=settings.silence_threshold,
        silence_chunks_needed=settings.silence_chunks_needed,
        speech_start_threshold=settings.speech_start_threshold,
        sample_rate=settings.sample_rate,
        max_utterance_seconds=settings.max_utterance_seconds,
    )
