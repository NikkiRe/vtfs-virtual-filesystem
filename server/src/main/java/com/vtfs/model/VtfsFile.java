package com.vtfs.model;

import jakarta.persistence.*;
import java.util.List;

@Entity
@Table(name = "vtfs_files", indexes = {
    @Index(name = "idx_parent_ino", columnList = "parent_ino"),
    @Index(name = "idx_ino", columnList = "ino"),
    @Index(name = "idx_token_parent_name", columnList = "token,parent_ino,name")
})
public class VtfsFile {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    
    @Column(nullable = false)
    private String token;
    
    @Column(nullable = false)
    private Long ino;
    
    @Column(nullable = false, length = 256)
    private String name;
    
    @Column(nullable = false)
    private Long parentIno;
    
    @Column(nullable = false)
    private Integer mode;
    
    @Column(nullable = false)
    private Integer nlink = 1;
    
    @Column(nullable = false)
    private Long dataSize = 0L;
    
    @OneToMany(mappedBy = "file", cascade = CascadeType.ALL, orphanRemoval = true)
    private List<FileData> fileData;
    
    public VtfsFile() {}
    
    public VtfsFile(String token, Long ino, String name, Long parentIno, Integer mode) {
        this.token = token;
        this.ino = ino;
        this.name = name;
        this.parentIno = parentIno;
        this.mode = mode;
        this.nlink = 1;
        this.dataSize = 0L;
    }
    
    public boolean isDirectory() {
        return (mode & 0040000) != 0;
    }
    
    public boolean isRegularFile() {
        return (mode & 0100000) != 0;
    }
    
    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }
    
    public String getToken() { return token; }
    public void setToken(String token) { this.token = token; }
    
    public Long getIno() { return ino; }
    public void setIno(Long ino) { this.ino = ino; }
    
    public String getName() { return name; }
    public void setName(String name) { this.name = name; }
    
    public Long getParentIno() { return parentIno; }
    public void setParentIno(Long parentIno) { this.parentIno = parentIno; }
    
    public Integer getMode() { return mode; }
    public void setMode(Integer mode) { this.mode = mode; }
    
    public Integer getNlink() { return nlink; }
    public void setNlink(Integer nlink) { this.nlink = nlink; }
    
    public Long getDataSize() { return dataSize; }
    public void setDataSize(Long dataSize) { this.dataSize = dataSize; }
    
    public List<FileData> getFileData() { return fileData; }
    public void setFileData(List<FileData> fileData) { this.fileData = fileData; }
}

