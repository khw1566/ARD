import subprocess
import os
from pathlib import Path

def convert_d2_files(directory_path):
    """
    지정된 디렉토리 내의 모든 .d2 파일을 .svg 및 .png 파일로 변환합니다.
    """
    folder = Path(directory_path)
    d2_files = list(folder.glob("*.d2"))

    if not d2_files:
        print("작업 대상 폴더에 .d2 파일이 존재하지 않습니다.")
        return

    print(f"총 {len(d2_files)}개의 .d2 파일 변환을 시작합니다.\n")
    print("-" * 40)

    for d2_file in d2_files:
        file_name = d2_file.stem
        
        svg_file = folder / f"{file_name}.svg"
        png_file = folder / f"{file_name}.png"

        print(f"대상 파일: {d2_file.name}")

        # SVG 포맷 변환
        try:
            subprocess.run(["d2", str(d2_file), str(svg_file)], check=True, capture_output=True)
            print(f"  [성공] {svg_file.name} 생성 완료")
        except subprocess.CalledProcessError as e:
            error_msg = e.stderr.decode('utf-8', errors='ignore').strip()
            print(f"  [실패] SVG 변환 오류: {error_msg}")

        # PNG 포맷 변환
        try:
            subprocess.run(["d2", str(d2_file), str(png_file)], check=True, capture_output=True)
            print(f"  [성공] {png_file.name} 생성 완료")
        except subprocess.CalledProcessError as e:
            error_msg = e.stderr.decode('utf-8', errors='ignore').strip()
            print(f"  [실패] PNG 변환 오류: {error_msg}")
        
        print("-" * 40)
    
    print("\n일괄 변환 프로세스가 종료되었습니다.")

if __name__ == "__main__":
    # 스크립트 파일이 위치한 현재 디렉토리를 절대 경로로 자동 인식합니다.
    current_directory = os.path.dirname(os.path.abspath(__file__))
    
    # 변환 함수 실행
    convert_d2_files(current_directory)
    
    # 터미널 창이 즉시 종료되는 것을 방지하여 사용자가 결과를 확인할 수 있도록 대기합니다.
    input("\n작업을 완료했습니다. 엔터 키를 누르면 창이 닫힙니다...")