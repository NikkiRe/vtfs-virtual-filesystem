package com.vtfs.model;

import jakarta.persistence.*;

@Entity
@Table(name = "vtfs_file_data", indexes = {
    @Index(name = "idx_token_ino", columnList = "token,ino")
})
public class FileData {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(nullable = false)
    private String token;
    
    @Column(nullable = false)
    private Long ino;
    
    @Column(name = "\"offset\"", nullable = false)
    private Long offset;
    
    @Column(nullable = false, columnDefinition = "BYTEA")
    private byte[] data;
    
    @ManyToOne(fetch = FetchType.LAZY, optional = true)
    @JoinColumn(name = "file_id", nullable = true)
    private VtfsFile file;
    
    public FileData() {}
    
    public FileData(String token, Long ino, Long offset, byte[] data) {
        this.token = token;
        this.ino = ino;
        this.offset = offset;
        this.data = data;
    }
    
    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }
    
    public String getToken() { return token; }
    public void setToken(String token) { this.token = token; }
    
    public Long getIno() { return ino; }
    public void setIno(Long ino) { this.ino = ino; }
    
    public Long getOffset() { return offset; }
    public void setOffset(Long offset) { this.offset = offset; }
    
    public byte[] getData() { return data; }
    public void setData(byte[] data) { this.data = data; }
    
    public VtfsFile getFile() { return file; }
    public void setFile(VtfsFile file) { this.file = file; }
}

