import cv2
import os

video_path = r"C:\Users\austin\.gemini\antigravity\brain\4a17d844-f139-4144-a16f-63b87a343ee7\.user_uploaded\media_1794328306970_861afea2.mp4"
output_dir = r"C:\Users\austin\.gemini\antigravity\brain\4a17d844-f139-4144-a16f-63b87a343ee7\video_frames"

cap = cv2.VideoCapture(video_path)
fps = cap.get(cv2.CAP_PROP_FPS)

for sec in range(1, 10):
    cap.set(cv2.CAP_PROP_POS_FRAMES, int(sec * fps))
    ret, frame = cap.read()
    if ret:
        cv2.imwrite(os.path.join(output_dir, f"sec_{sec}.jpg"), frame)

cap.release()
print("Extracted sec 1 to 9")
