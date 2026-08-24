# Get .pkg files from the current directory
$sourceDir = "$(Split-Path -Parent $MyInvocation.MyCommand.Path)"
$targetDir = "E:\Nports\7015_linux_imgs"

# Check if target directory exists, create if it doesn't
if (!(Test-Path $targetDir)) {
    Write-Host "Creating target directory: $targetDir" -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $targetDir -Force
}

# Get all .pkg files
$pkgFiles = Get-ChildItem -Path $sourceDir -Filter "*.pkg" -File

if ($pkgFiles.Count -eq 0) {
    Write-Host "No .pkg files found" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 0
}

Write-Host "Found $($pkgFiles.Count) .pkg file(s)" -ForegroundColor Green

# Copy each .pkg file
foreach ($file in $pkgFiles) {
    Write-Host "`nCopying file: $($file.Name)" -ForegroundColor Cyan
    
    # Get file size
    $fileSize = $file.Length
    $fileSizeKB = [math]::Round($fileSize / 1KB, 2)
    $fileSizeMB = [math]::Round($fileSize / 1MB, 2)
    
    if ($fileSizeMB -gt 0) {
        Write-Host "File size: $fileSizeMB MB ($fileSizeKB KB)" -ForegroundColor White
    } else {
        Write-Host "File size: $fileSizeKB KB" -ForegroundColor White
    }
    
    $targetPath = Join-Path $targetDir $file.Name
    
    # Copy file and show progress
    Write-Host "Copying to: $targetPath" -ForegroundColor White
    
    # Use Copy-Item command to copy file
    try {
        Copy-Item -Path $file.FullName -Destination $targetDir -Force
        Write-Host "Copy successful" -ForegroundColor Green
    } catch {
        Write-Host "Copy failed: $($_.Exception.Message)" -ForegroundColor Red
    }
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "Copy completed" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "Copied files are saved to: $targetDir" -ForegroundColor White

Read-Host "Press Enter to exit"