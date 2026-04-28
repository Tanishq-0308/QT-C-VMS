-- 🔹 Create table for doctors
CREATE TABLE IF NOT EXISTS doctors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    specialization TEXT,
    contact TEXT
);

-- 🔹 Insert a sample doctor (only if not already present)
INSERT INTO doctors (name, specialization, contact)
SELECT 'Dr. John Watson', 'General Medicine', '9876543210'
WHERE NOT EXISTS (
    SELECT 1 FROM doctors WHERE name = 'Dr. John Watson'
);



-- 🔹 Create table for patients
CREATE TABLE IF NOT EXISTS patients (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    patient_id TEXT UNIQUE NOT NULL,   -- 👈 Ensure uniqueness
    mobile TEXT,
    dob TEXT,
    gender TEXT,
    address TEXT
);

-- 🔹 Insert a sample patient (only if not already present)
INSERT INTO patients (
    first_name, last_name, patient_id, mobile, dob, gender, address
)
SELECT 'Alice', 'Smith', 'P1001', '9998887777', '1995-08-20', 'Female', '123 Main Street'
WHERE NOT EXISTS (
    SELECT 1 FROM patients WHERE patient_id = 'P1001'
);





-- 🔹 Create table for surgeries

CREATE TABLE IF NOT EXISTS surgeries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    patient_id TEXT NOT NULL,
    surgeon_name TEXT NOT NULL,
    surgery_type TEXT NOT NULL,
    body_part TEXT,
    additional_surgeon TEXT,
    anesthesiologist TEXT,
    surgery_date TEXT NOT NULL,
    operation_theatre_number TEXT,
    surgical_history TEXT,
    FOREIGN KEY (patient_id) REFERENCES patients(id)
);

-- 🔹 Insert a sample surgery
INSERT INTO surgeries (
    patient_id, surgeon_name, surgery_type, body_part,
    additional_surgeon, anesthesiologist, surgery_date,
    operation_theatre_number, surgical_history
)
SELECT 'P1001', 'Dr. John Watson', 'Appendectomy', 'Abdomen', 'Dr. Sarah Lee',
       'Dr. Ana Rao', '2025-05-23', 'OT-3', 'Previous appendicitis case'
WHERE NOT EXISTS (
    SELECT 1 FROM surgeries WHERE patient_id = 'P1001' AND surgery_type = 'Appendectomy'
);



CREATE TABLE IF NOT EXISTS company (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT,
    phone TEXT
);

-- Optional dummy insert
INSERT INTO company (name, address, phone)
SELECT 'Brainwave Medical Systems', '123 Innovation Street', '0123456789'
WHERE NOT EXISTS (
    SELECT 1 FROM company WHERE name = 'Brainwave Medical Systems'
);


CREATE TABLE IF NOT EXISTS comments_image (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    image_id INTEGER NOT NULL,
    text TEXT,
    FOREIGN KEY(image_id) REFERENCES snapshots(id)
);

-- Optional dummy comment
INSERT INTO comments_image (image_id, text)
SELECT 1, 'Initial comment for testing'
WHERE NOT EXISTS (
    SELECT 1 FROM comments_image WHERE image_id = 1 AND text = 'Initial comment for testing'
);


CREATE TABLE IF NOT EXISTS comments_video (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    video_id INTEGER NOT NULL,
    text TEXT,
    FOREIGN KEY(video_id) REFERENCES recordings(id)
);

-- Optional dummy comment
INSERT INTO comments_video (video_id, text)
SELECT 1, 'Test video comment'
WHERE NOT EXISTS (
    SELECT 1 FROM comments_video WHERE video_id = 1 AND text = 'Test video comment'
);


CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL
);

-- Dummy entry
INSERT INTO users (name, email)
SELECT 'Default User', 'default@example.com'
WHERE NOT EXISTS (
    SELECT 1 FROM users WHERE name = 'Default User'
);



CREATE TABLE IF NOT EXISTS settings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    hospital_logo_path TEXT,
    hospital_name TEXT NOT NULL,
    software_name TEXT NOT NULL,
    hospital_email TEXT,
    hospital_address TEXT,
    state TEXT,
    district TEXT,
    pin TEXT,
    about_hospital TEXT,
    watermark_logo_path TEXT,
    storage_path TEXT,
    hospital_phone TEXT,
    rtsp_link TEXT,
    video_input TEXT DEFAULT 'SDI',
    show_video_label INTEGER DEFAULT 1
);

INSERT INTO settings (
    hospital_logo_path, hospital_name, software_name, hospital_email, hospital_address,
    state, district, pin, about_hospital, watermark_logo_path, storage_path, hospital_phone, rtsp_link, video_input, show_video_label
) VALUES (
    '/logo.png', 'Brainwave', 'VMS', 'cognet@gmail.com', 'Delhi',
    'Delhi', 'Okhla', '803119', 'Hospital description goes here',
    '/watermark.png', '/home/brainwave/app/store', '9876543210', 'rtsp://admin:123456@192.168.1.88/stream0', 'SDI', 1
);


CREATE TABLE IF NOT EXISTS snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    patient_id TEXT,
    surgery_id INTEGER,
    file_path TEXT,
    title TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(surgery_id) REFERENCES surgeries(id)
);

CREATE TABLE IF NOT EXISTS recordings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    patient_id TEXT,
    surgery_id INTEGER,
    file_path TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(surgery_id) REFERENCES surgeries(id)
);
