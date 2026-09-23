import sqlite3
import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas
from reportlab.lib import utils, colors

PAGE_WIDTH, PAGE_HEIGHT = A4
LEFT_MARGIN = 20 * mm
RIGHT_MARGIN = 20 * mm
TOP_MARGIN = 20 * mm
BOTTOM_MARGIN = 20 * mm
LINE_HEIGHT = 6  # mm


def fetch_data_from_db(db_path, patient_id, surgery_id):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    # Updated query to include state, district, pin, about_hospital
    cursor.execute("""
        SELECT hospital_logo_path, hospital_name, hospital_email, hospital_phone, 
               hospital_address, state, district, pin, about_hospital 
        FROM settings ORDER BY id LIMIT 1
    """)
    settings = cursor.fetchone()

    if settings:
        # Build path to logo inside build directory
        build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build"))
        new_logo_path = os.path.join(build_dir, "logo.png")
    
        if os.path.exists(new_logo_path):
            fixed_logo_path = new_logo_path
        else:
            base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
            db_logo_path = os.path.join(base_dir, settings[0]) if settings[0] else ""
            fixed_logo_path = db_logo_path if os.path.exists(db_logo_path) else None
    
        # Build full address from address, district, state, pin
        address = settings[4] or ""
        state = settings[5] or ""
        district = settings[6] or ""
        pin = settings[7] or ""
        about_hospital = settings[8] or ""
        
        # Combine into full address
        address_parts = [part for part in [address, district, state] if part]
        full_address = ", ".join(address_parts)
        if pin:
            full_address += f" - {pin}"
        
        # Reconstruct settings tuple: (logo, name, email, phone, full_address, about_hospital)
        settings = (fixed_logo_path, settings[1], settings[2], settings[3], full_address, about_hospital)
    else:
        settings = (None, "", "", "", "", "")

    cursor.execute("SELECT first_name, last_name, mobile, dob, gender, address FROM patients WHERE patient_id = ?", (patient_id,))
    patient = cursor.fetchone()

    cursor.execute("""
        SELECT surgeon_name, surgery_type, body_part, additional_surgeon, anesthesiologist,
               surgery_date, operation_theatre_number, surgical_history
        FROM surgeries
        WHERE patient_id = ? AND id = ?
    """, (patient_id, surgery_id))
    surgery = cursor.fetchone()

    cursor.execute("""
        SELECT s.file_path, GROUP_CONCAT(c.text, CHAR(10))
        FROM snapshots s
        LEFT JOIN comments_image c ON s.id = c.image_id
        WHERE s.patient_id = ? AND s.surgery_id = ?
        GROUP BY s.id
    """, (patient_id, surgery_id))
    snapshots = cursor.fetchall()

    conn.close()
    return settings, patient, surgery, snapshots


def ensure_space(c, current_y, needed_space_mm):
    if current_y - needed_space_mm < BOTTOM_MARGIN / mm:
        c.showPage()
        c._page_number += 1
        return (PAGE_HEIGHT - TOP_MARGIN) / mm
    return current_y


def draw_header(c, settings):
    logo_path, name, email, phone, address, about_hospital = settings
    c.setFillColor(colors.darkblue)
    c.setFont("Helvetica", 11)
    c.drawString(LEFT_MARGIN, PAGE_HEIGHT - 30 * mm, name)

    # Debug print to verify logo path
    print("Logo path:", logo_path)
    print("Logo exists:", os.path.exists(logo_path) if logo_path else False)

    if logo_path and os.path.exists(logo_path):
        try:
            img = utils.ImageReader(logo_path)
            c.drawImage(img, PAGE_WIDTH - RIGHT_MARGIN - 30 * mm, PAGE_HEIGHT - 40 * mm, width=30 * mm,
                        preserveAspectRatio=True, mask='auto')
        except Exception as e:
            print(f"Warning: Failed to load hospital logo: {e}")

    c.setFont("Helvetica", 10)
    c.setFillColor(colors.black)
    c.drawString(LEFT_MARGIN, PAGE_HEIGHT - 35 * mm, f"Email: {email or 'N/A'}")
    c.drawString(LEFT_MARGIN, PAGE_HEIGHT - 40 * mm, f"Phone: {phone or 'N/A'}")
    c.drawString(LEFT_MARGIN, PAGE_HEIGHT - 45 * mm, f"Address: {address or 'N/A'}")
    if about_hospital:
        c.drawString(LEFT_MARGIN, PAGE_HEIGHT - 50 * mm, f"About: {about_hospital}")
    c.setStrokeColor(colors.grey)
    c.setLineWidth(1)
    c.line(LEFT_MARGIN, PAGE_HEIGHT - 55 * mm, PAGE_WIDTH - RIGHT_MARGIN, PAGE_HEIGHT - 55 * mm)


def draw_section_title(c, title, y):
    padding_top = 3
    padding_bottom = 3
    padding_left = 4 * mm
    text_height = 10
    box_height = text_height + padding_top + padding_bottom

    y = ensure_space(c, y, box_height / mm + 2)
    rect_y = y * mm - padding_bottom
    c.setFillColorRGB(0.2, 0.4, 0.7)
    c.rect(LEFT_MARGIN, rect_y, PAGE_WIDTH - LEFT_MARGIN - RIGHT_MARGIN, box_height, fill=1, stroke=0)

    c.setFillColor(colors.white)
    c.setFont("Helvetica-Bold", 11)
    text_y = rect_y + padding_bottom + (text_height * 0.25)
    c.drawString(LEFT_MARGIN + padding_left, text_y, title)

    c.setFillColor(colors.black)
    return y - (box_height / mm) - 2


def draw_label_value_pairs(c, labels, values, y):
    c.setFont("Helvetica", 10)
    for label, value in zip(labels, values):
        y = ensure_space(c, y, LINE_HEIGHT)
        c.drawString(LEFT_MARGIN + 5 * mm, y * mm, f"{label}: {value}")
        y -= LINE_HEIGHT
    return y

def draw_snapshots(c, snapshots, y_start):
    max_width = 60 * mm
    max_height = 45 * mm
    image_x = LEFT_MARGIN + 5 * mm
    comment_x = image_x + max_width + 10 * mm
    box_padding = 5 * mm
    gap_between_boxes = 8 * mm
    line_spacing = 12  # px spacing between comment lines

    y = y_start

    for img_path, comment_text in snapshots:
        if not os.path.exists(img_path):
            print(f"Warning: Snapshot image not found: {img_path}")
            continue

        try:
            img = utils.ImageReader(img_path)
            iw, ih = img.getSize()
            scale = min(max_width / iw, max_height / ih, 1.0)
            iw_scaled = iw * scale
            ih_scaled = ih * scale

            # Split and clean comments
            comment_lines = [line.strip() for line in comment_text.strip().split('\n') if line.strip()] if comment_text else ["No comment"]
            comment_height = len(comment_lines) * line_spacing

            image_height_mm = ih_scaled / mm
            box_height_mm = max(image_height_mm, comment_height / mm) + (2 * box_padding / mm)

            # Ensure space
            y = ensure_space(c, y, box_height_mm + gap_between_boxes / mm)

            box_y = y * mm - box_height_mm * mm
            c.setStrokeColor(colors.grey)
            c.setLineWidth(0.5)
            c.rect(LEFT_MARGIN, box_y, PAGE_WIDTH - LEFT_MARGIN - RIGHT_MARGIN, box_height_mm * mm, stroke=1, fill=0)

            # Draw image
            image_y = box_y + (box_height_mm * mm - ih_scaled) / 2  # Center vertically
            c.drawImage(img, image_x, image_y, width=iw_scaled, height=ih_scaled, preserveAspectRatio=True, mask='auto')

            # Draw comment lines
            comment_y = box_y + box_height_mm * mm - box_padding
            c.setFont("Helvetica", 10)
            for line in comment_lines:
                c.drawString(comment_x, comment_y, line)
                comment_y -= line_spacing

            # Move Y for next box
            y -= (box_height_mm + gap_between_boxes / mm)

        except Exception as e:
            print(f"Warning: Failed to load image {img_path}: {e}")

    return y




def generate_pdf_report(db_path, patient_id, surgery_id, output_pdf_path):
    try:
        settings, patient, surgery, snapshots = fetch_data_from_db(db_path, patient_id, surgery_id)
        if not all([settings, patient, surgery]):
            return False, "Missing data for PDF generation"

        c = canvas.Canvas(output_pdf_path, pagesize=A4)
        c._settings = settings
        c._page_number = 1

        draw_header(c, settings)
        y = (PAGE_HEIGHT - 60 * mm) / mm  # Adjusted for taller header with about_hospital

        y = draw_section_title(c, "Patient Details", y)
        y = draw_label_value_pairs(c,
                                   ["Name", "Mobile", "DOB", "Gender", "Address"],
                                   [f"{patient[0]} {patient[1]}", patient[2], patient[3], patient[4], patient[5]],
                                   y)

        y = draw_section_title(c, "Surgery Details", y)
        y = draw_label_value_pairs(c,
                                   ["Surgeon", "Surgery Type", "Body Part", "Additional Surgeon", "Anesthesiologist",
                                    "Surgery Date", "OT Number", "Surgical History"],
                                   list(surgery),
                                   y)

        if snapshots:
            y = draw_section_title(c, "Surgical Snapshots", y)
            y = draw_snapshots(c, snapshots, y)

        c.save()
        return True, output_pdf_path

    except Exception as e:
        return False, str(e)


# Example usage
if __name__ == "__main__":
    db_path = "../sqlite.db"  # Change to your DB path
    patient_id = "P1001"       # Change to your patient ID
    surgery_id = 1             # Change to your surgery ID
    output_pdf_path = "styled_surgery_report.pdf"

    success, msg = generate_pdf_report(db_path, patient_id, surgery_id, output_pdf_path)
    if success:
        print(f"✅ PDF report generated: {msg}")
    else:
        print(f"❌ Error generating PDF: {msg}")