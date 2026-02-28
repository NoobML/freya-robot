from google import genai
from google.genai import types
import logging
import re
from python.api.API_KEYS import API_KEYS
from config import settings
from typing import List

logger = logging.getLogger("llm_service")


def count_tokens(text: str) -> int:
    return len(text) // 4  # rough estimate, adjust if needed


def trim_history(history: List[str], max_tokens: int = None) -> List[str]:
    max_tokens = max_tokens or settings.MAX_CONVERSATION_TOKENS
    total_tokens = sum(count_tokens(msg) for msg in history)
    while total_tokens > max_tokens and history:
        removed = history.pop(0)
        total_tokens -= count_tokens(removed)
    return history


class LLMService:
    def __init__(self, model_name=None, system_prompt=None):
        self.model_name = model_name or settings.LLM_MODEL
        self.system_prompt = system_prompt or settings.SYSTEM_PROMPT
        self.client = None
        self.active_key = None
        self.init_client()
        self.conversation_history: List[str] = []
        if self.system_prompt:
            self.conversation_history.append(self.system_prompt)
        logger.info(f"LLM Service initialized with {self.model_name}")

    def init_client(self):
        for key in API_KEYS:
            try:
                client = genai.Client(api_key=key)
                client.models.list()  # lightweight test
                self.client = client
                self.active_key = key
                logger.info(f"Using API key: {key}")
                break
            except Exception as e:
                logger.warning(f"API key failed: {key} | {str(e)}")
        if not self.client:
            raise Exception("No valid API key available!")

    def _remove_emojis(self, text: str) -> str:
        """Remove emojis and emoticons from text"""
        # Remove emoji unicode characters
        emoji_pattern = re.compile(
            "["
            "\U0001F600-\U0001F64F"  # emoticons
            "\U0001F300-\U0001F5FF"  # symbols & pictographs
            "\U0001F680-\U0001F6FF"  # transport & map
            "\U0001F1E0-\U0001F1FF"  # flags
            "\U00002702-\U000027B0"
            "\U000024C2-\U0001F251"
            "]+",
            flags=re.UNICODE
        )
        text = emoji_pattern.sub('', text)

        # Remove text emoticons like :) :( etc
        text = re.sub(r'[:;=]-?[)(DPp/\\]', '', text)

        return text.strip()

    def generate_response(self, user_message: str) -> tuple[str, str]:
        """
        Generate response and extract emotion

        Returns:
            tuple: (response_text, emotion)
        """
        # Append user message
        self.conversation_history.append(user_message)

        # Trim history
        self.conversation_history = trim_history(self.conversation_history)

        response_text = ""  # ← Initialize to avoid UnboundLocalError

        try:
            response = self.client.models.generate_content(
                model=self.model_name,
                contents=self.conversation_history
            )
            response_text = response.text

            # Remove emojis
            response_text = self._remove_emojis(response_text)

            # Extract emotion and clean text
            text, emotion = self._extract_emotion(response_text)

            logger.info(f"Generated Response: {text[:100]} | Emotion: {emotion}")

            # Store the FULL response in history for context
            self.conversation_history.append(response_text)

            return text, emotion

        except Exception as e:
            error_msg = str(e)
            logger.error(f"LLM Generation error: {error_msg}")

            # Check if it's a quota error
            if "429" in error_msg or "RESOURCE_EXHAUSTED" in error_msg or "quota" in error_msg.lower():
                logger.warning("Quota exceeded, trying next API key...")

                current_key = self.active_key
                if current_key in API_KEYS:
                    remaining_keys = [k for k in API_KEYS if k != current_key]

                    # Try remaining keys
                    for key in remaining_keys:
                        try:
                            client = genai.Client(api_key=key)
                            response = client.models.generate_content(
                                model=self.model_name,
                                contents=self.conversation_history
                            )
                            response_text = response.text
                            response_text = self._remove_emojis(response_text)

                            # Extract emotion
                            text, emotion = self._extract_emotion(response_text)

                            # Success! Update to new key
                            self.client = client
                            self.active_key = key
                            logger.info(f"Switched to API key: {key}")

                            self.conversation_history.append(response_text)
                            return text, emotion

                        except Exception as retry_error:
                            logger.warning(f"Key {key} also failed: {str(retry_error)}")
                            continue

                    # All keys exhausted
                    raise Exception("All API keys exhausted. Please wait or add more keys.")

            # Not a quota error, re-raise
            raise

    def _extract_emotion(self, text: str) -> tuple[str, str]:
        """
        Extract emotion from response text

        Args:
            text: Response text with emotion in brackets

        Returns:
            tuple: (cleaned_text, emotion)
        """
        import re

        # Match emotion at end: (happy), (wave), etc.
        emotion_match = re.search(r'\((\w+)\)\s*$', text.strip())

        if emotion_match:
            emotion = emotion_match.group(1).lower()
            clean_text = text[:emotion_match.start()].strip()
        else:
            emotion = "neutral"
            clean_text = text

        return clean_text, emotion


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)

    llm = LLMService()

    response = llm.generate_response("Hello, What's your name?")
    print(f"Response: {response}")