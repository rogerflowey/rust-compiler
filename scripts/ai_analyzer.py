# scripts/ai_analyzer.py
import os
import re
import time
from zai import ZhipuAiClient

# --- Configuration ---
client = ZhipuAiClient(api_key=os.environ.get("ZHIPU_API_KEY"))
AI_LOG_DIR = "../test-output/ai-logs"
# ---------------------

def load_prompt(prompt_file="prompts/parser_verifier.md"):
    """Loads the system prompt from a file."""
    try:
        with open(prompt_file, 'r', encoding='utf-8') as f:
            return f.read()
    except FileNotFoundError:
        print(f"Error: Prompt file not found at '{prompt_file}'. Make sure it exists.")
        return None 

def parse_analysis_response(response_text):
    """
    Parses the structured response from the AI to extract verdicts.
    This version is robust against markdown code fences (```) in the response.
    """
    results = {}
    
    # --- NEW: Pre-process the response to remove markdown fences ---
    cleaned_text = response_text.strip()
    if cleaned_text.startswith("```") and cleaned_text.endswith("```"):
        print("  -> Stripping markdown code fences from AI response.")
        # Split into lines, remove the first and last line, then join back
        lines = cleaned_text.split('\n')
        if len(lines) > 2:
            cleaned_text = '\n'.join(lines[1:-1])
        else: # Handle case of empty or single-line content within fences
            cleaned_text = ""
    
    # Regex to capture the test name, verdict, and reason for each block
    pattern = re.compile(
        r"Test-Case:\s*(.*?)\s*\n"      # Capture test name
        r"Verdict:\s*(.*?)\s*\n"        # Capture verdict
        r"Reason:\s*([\s\S]*?)\n---",   # Capture multi-line reason, until the ---
        re.MULTILINE
    )

    matches = pattern.finditer(cleaned_text)
    for match in matches:
        test_name = match.group(1).strip()
        verdict = match.group(2).strip().upper()
        reason = match.group(3).strip()

        is_correct = (verdict == "CORRECT")
        results[test_name] = (is_correct, reason)

    return results

def analyze_batch_with_ai(batch_report_content, stage, batch_num):
    """
    Analyzes a batch of test cases using the Zhipu AI API and logs the raw response.
    """
    print(f"Analyzing batch {batch_num} with Zhipu AI...")

    system_prompt = load_prompt()
    if not system_prompt:
        return {}

    try:
        print(f"  -> Sending request for batch {batch_num}...")
        response = client.chat.completions.create(
            model="glm-4.5",
            messages=[
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": f"Here are the test cases to analyze:\n\n{batch_report_content}"}
            ],
            max_tokens=8192,
            temperature=0.2
        )
        print(f"  -> Received response for batch {batch_num}.")
        
        raw_response_text = ""
        if response.choices:
            raw_response_text = response.choices[0].message.content
        else:
            print(f"  -> WARNING: No response choices received for batch {batch_num}.")
            raw_response_text = "Error: No response choices received from API."

        os.makedirs(AI_LOG_DIR, exist_ok=True)
        log_file_path = os.path.join(AI_LOG_DIR, f"{stage}_batch_{batch_num}_response.log")
        print(f"  -> Saving full AI response for batch {batch_num} to {log_file_path}")
        try:
            with open(log_file_path, 'w', encoding='utf-8') as f:
                f.write("--- API Response ---\n")
                f.write(raw_response_text)
                if response.usage:
                    f.write("\n\n--- Usage Info ---\n")
                    f.write(str(response.usage))
        except Exception as log_e:
            print(f"  -> ERROR: Could not write to log file {log_file_path}: {log_e}")

        if not response.choices:
            return {}

        return parse_analysis_response(raw_response_text)

    except Exception as e:
        print(f"  -> ERROR during Zhipu AI API call for batch {batch_num}: {e}")
        os.makedirs(AI_LOG_DIR, exist_ok=True)
        log_file_path = os.path.join(AI_LOG_DIR, f"{stage}_batch_{batch_num}_error.log")
        with open(log_file_path, 'w', encoding='utf-8') as f:
            f.write(f"An exception occurred during the API call for batch {batch_num}:\n\n{e}")
        print(f"  -> Exception details saved to {log_file_path}")
        return {}